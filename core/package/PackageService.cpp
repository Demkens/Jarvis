#include "PackageService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDebug>

#include "common/Timestamps.h"
#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "storage/StorageService.h"

namespace {

// 内部检查统一走局部错误对象，出错时向调用方的 error 指针（可为空）转述
bool failed(const AppError &local, AppError *out)
{
    if (local.ok())
        return false;
    if (out != nullptr)
        *out = local;
    return true;
}

void setOut(AppError *out, AppError::Code code, const QString &message)
{
    if (out != nullptr)
        *out = AppError::fail(code, message);
}

} // namespace

PackageService::PackageService(AppPaths *paths, LibraryService *library, StorageService *storage,
                               EventBus *events)
    : m_paths(paths)
    , m_library(library)
    , m_storage(storage)
    , m_events(events)
{
}

bool PackageService::requireCurrentLibrary(QString *libraryName, QString *linkAddress,
                                           AppError *error) const
{
    const QString name = m_library->currentName();
    if (name.isEmpty()) {
        setOut(error, AppError::Validation, QStringLiteral("没有打开的库"));
        return false;
    }
    if (libraryName != nullptr)
        *libraryName = name;
    if (linkAddress != nullptr)
        *linkAddress = m_library->config().entry(name).linkAddress;
    return true;
}

QVariantList PackageService::listPackages(const QString &form, AppError *error) const
{
    QString sql =
        QStringLiteral("SELECT f.id, f.title, f.score, f.size, f.storage_path, "
                       "f.created_time, f.updated_time, t.form AS type_form "
                       "FROM file f JOIN type t ON t.id = f.type_id");
    QVariantMap params;
    if (!form.isEmpty()) {
        sql += QStringLiteral(" WHERE t.form = :form");
        params.insert(QStringLiteral("form"), form);
    }
    sql += QStringLiteral(" ORDER BY f.id");

    AppError local;
    const QList<QVariantMap> rows = m_library->dbAccess().select(sql, params, &local);
    if (failed(local, error))
        return {};

    QVariantList list;
    for (const QVariantMap &row : rows)
        list.append(row);
    return list;
}

QVariantMap PackageService::packageDetail(qint64 id, AppError *error) const
{
    AppError local;
    DbAccess &db = m_library->dbAccess();

    QVariantMap row;
    const bool found = db.selectOne(
        QStringLiteral("SELECT f.*, t.form AS type_form FROM file f "
                       "JOIN type t ON t.id = f.type_id WHERE f.id = :id"),
        {{QStringLiteral("id"), id}}, row, &local);
    if (failed(local, error))
        return {};
    if (!found) {
        setOut(error, AppError::Validation, QStringLiteral("数据包不存在：#%1").arg(id));
        return {};
    }

    // 分表字段（表名来自本库 type 表登记，非用户输入；仍校验安全标识符兜底）
    const QString form = row.value(QStringLiteral("type_form")).toString();
    if (StorageService::isSafeFormName(form)) {
        const int exists = db.scalar(
            QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = :n"),
            {{QStringLiteral("n"), form}}, &local).toInt();
        if (failed(local, error))
            return {};

        if (exists == 1) {
            QVariantMap typeRow;
            const bool typeFound = db.selectOne(
                QStringLiteral("SELECT * FROM %1 WHERE id = :id").arg(form),
                {{QStringLiteral("id"), id}}, typeRow, &local);
            if (failed(local, error))
                return {};
            if (typeFound) {
                for (auto it = typeRow.constBegin(); it != typeRow.constEnd(); ++it) {
                    if (it.key() != QStringLiteral("id"))
                        row.insert(it.key(), it.value());
                }
            }
        }

        // 创作者显示名（id=0 为 anonymous）
        if (row.contains(QStringLiteral("creator_id"))) {
            const qint64 creatorId = row.value(QStringLiteral("creator_id")).toLongLong();
            if (creatorId > 0) {
                const QString name = db.scalar(
                    QStringLiteral("SELECT name FROM creator WHERE id = :id"),
                    {{QStringLiteral("id"), creatorId}}, &local).toString();
                if (failed(local, error))
                    return {};
                row.insert(QStringLiteral("creatorName"), name);
            } else {
                row.insert(QStringLiteral("creatorName"), QStringLiteral("anonymous"));
            }
        }
    }
    return row;
}

bool PackageService::updateTitle(qint64 id, const QString &title, AppError *error)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) {
        setOut(error, AppError::Validation, QStringLiteral("标题不能为空"));
        return false;
    }

    AppError local;
    DbAccess &db = m_library->dbAccess();
    QVariantMap row;
    const bool found = db.selectOne(QStringLiteral("SELECT id FROM file WHERE id = :id"),
                                    {{QStringLiteral("id"), id}}, row, &local);
    if (failed(local, error))
        return false;
    if (!found) {
        setOut(error, AppError::Validation, QStringLiteral("数据包不存在：#%1").arg(id));
        return false;
    }

    if (!db.execute(QStringLiteral("UPDATE file SET title = :title, updated_time = :now WHERE id = :id"),
                    {{QStringLiteral("title"), trimmed},
                     {QStringLiteral("now"), isoNowUtc()},
                     {QStringLiteral("id"), id}},
                    error))
        return false;

    emit m_events->packageUpdated(id);
    return true;
}

bool PackageService::ratePackage(qint64 id, int newScore, AppError *error)
{
    if (newScore < 0 || newScore > 100) {
        setOut(error, AppError::Validation, QStringLiteral("评分需在 0–100 之间"));
        return false;
    }

    AppError local;
    DbAccess &db = m_library->dbAccess();
    QVariantMap row;
    const bool found = db.selectOne(QStringLiteral("SELECT score FROM file WHERE id = :id"),
                                    {{QStringLiteral("id"), id}}, row, &local);
    if (failed(local, error))
        return false;
    if (!found) {
        setOut(error, AppError::Validation, QStringLiteral("数据包不存在：#%1").arg(id));
        return false;
    }

    // 评分算法（开发文档 6.9）：首评直接生效；再评加权；0 = 未评分/清除
    const int current = row.value(QStringLiteral("score")).toInt();
    int result = newScore;
    if (current != 0 && newScore != 0)
        result = qRound(current * (1.0 - m_scoreRecentWeight)
                        + newScore * m_scoreRecentWeight);
    result = qBound(0, result, 100);

    if (!db.execute(QStringLiteral("UPDATE file SET score = :score, updated_time = :now WHERE id = :id"),
                    {{QStringLiteral("score"), result},
                     {QStringLiteral("now"), isoNowUtc()},
                     {QStringLiteral("id"), id}},
                    error))
        return false;

    emit m_events->packageUpdated(id);
    return true;
}

QString PackageService::browseState(qint64 id, AppError *error) const
{
    AppError local;
    QVariantMap row;
    const bool found = m_library->dbAccess().selectOne(
        QStringLiteral("SELECT position FROM browse_state WHERE file_id = :id"),
        {{QStringLiteral("id"), id}}, row, &local);
    if (failed(local, error))
        return QString();
    if (!found)
        return QString();
    return row.value(QStringLiteral("position")).toString();
}

bool PackageService::writeBrowseState(qint64 id, const QString &positionJson, AppError *error)
{
    const QString trimmed = positionJson.trimmed();
    if (!trimmed.isEmpty() && QJsonDocument::fromJson(trimmed.toUtf8()).isNull()) {
        setOut(error, AppError::Validation, QStringLiteral("浏览位置不是合法 JSON"));
        return false;
    }

    // UPSERT；浏览行为不触碰 file.updated_time（开发文档 6.4）
    return m_library->dbAccess().execute(
        QStringLiteral("INSERT INTO browse_state (file_id, position, updated_time) "
                       "VALUES (:id, :position, :now) "
                       "ON CONFLICT(file_id) DO UPDATE SET "
                       "position = excluded.position, updated_time = excluded.updated_time"),
        {{QStringLiteral("id"), id},
         {QStringLiteral("position"), trimmed},
         {QStringLiteral("now"), isoNowUtc()}},
        error);
}

bool PackageService::deletePackage(qint64 id, AppError *error)
{
    QString libraryName;
    QString linkAddress;
    if (!requireCurrentLibrary(&libraryName, &linkAddress, error))
        return false;

    AppError local;
    DbAccess &db = m_library->dbAccess();
    QVariantMap row;
    const bool found = db.selectOne(QStringLiteral("SELECT storage_path FROM file WHERE id = :id"),
                                    {{QStringLiteral("id"), id}}, row, &local);
    if (failed(local, error))
        return false;
    if (!found) {
        setOut(error, AppError::Validation, QStringLiteral("数据包不存在：#%1").arg(id));
        return false;
    }
    const int bucket = row.value(QStringLiteral("storage_path")).toInt();

    // 1. 事务删除 db 行（分表/browse_state/关联表靠外键级联，开发文档 6.10）
    if (!db.transaction(error))
        return false;
    if (!db.execute(QStringLiteral("DELETE FROM file WHERE id = :id"),
                    {{QStringLiteral("id"), id}}, error)) {
        db.rollback();
        return false;
    }
    if (!db.commit(error)) {
        db.rollback();
        return false;
    }

    // 2. 删包文件夹与封面；任一部分失败给出明确错误，不静默
    const QString packageDir = StorageService::packageDir(linkAddress, bucket, id);
    if (QFileInfo::exists(packageDir) && !QDir(packageDir).removeRecursively()) {
        setOut(error, AppError::Io,
               QStringLiteral("数据包文件删除失败（可能被占用或权限不足），数据库记录已删除：%1")
                   .arg(QDir::toNativeSeparators(packageDir)));
        return false;
    }

    const QString cover = StorageService::coverPath(m_paths->coverDir(libraryName), bucket, id);
    if (QFileInfo::exists(cover) && !QFile::remove(cover)) {
        setOut(error, AppError::Io,
               QStringLiteral("封面删除失败：%1").arg(QDir::toNativeSeparators(cover)));
        return false;
    }

    emit m_events->packageDeleted(id);
    qInfo().noquote() << "[package] 已删除包: id =" << id << "桶 =" << bucket;
    return true;
}
