#include "PackageService.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDebug>
#include <QUrl>

#include "common/Timestamps.h"
#include "creator/CreatorService.h"
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

// 转义 LIKE 通配符：\ % _ 前加反斜杠，配合 SQL 的 ESCAPE '\' 使关键词按字面匹配
QString escapeLike(const QString &raw)
{
    QString out;
    out.reserve(raw.size() + 4);
    for (const QChar ch : raw) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('%') || ch == QLatin1Char('_'))
            out.append(QLatin1Char('\\'));
        out.append(ch);
    }
    return out;
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
        QStringLiteral("SELECT f.id, f.title, f.score, f.size, f.storage_path, f.aspect_ratio, "
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

QVariantList PackageService::queryPackages(const QueryFilters &filters, int offset, int limit,
                                           int *total, AppError *error) const
{
    // WHERE 片段与参数（参数化防注入）：各条件为空即不生效
    QString where = QStringLiteral("1=1");
    QVariantMap params;
    if (!filters.form.isEmpty()) {
        where += QStringLiteral(" AND t.form = :form");
        params.insert(QStringLiteral("form"), filters.form);
    }
    const QString kw = filters.keyword.trimmed();
    if (!kw.isEmpty()) {
        // 通配符按字面匹配：转义 \ % _ 后两侧补 %，配合 ESCAPE '\'
        const QString pattern = QLatin1Char('%') + escapeLike(kw) + QLatin1Char('%');
        if (filters.scope == SearchCreator) {
            // 创作者范围：命中任一关联创作者的正式名或别名；
            // EXISTS 避免多创作者/多别名导致同一 file 重复行
            // （:kw_name / :kw_alias 用不同名，避免同一命名占位符重复绑定的兼容性问题）
            where += QStringLiteral(
                " AND EXISTS (SELECT 1 FROM file_creator fc JOIN creator c ON c.id = fc.creator_id "
                "WHERE fc.file_id = f.id AND (c.name LIKE :kw_name ESCAPE '\\' "
                "OR EXISTS (SELECT 1 FROM alias a WHERE a.id = c.id AND a.alias LIKE :kw_alias ESCAPE '\\')))");
            params.insert(QStringLiteral("kw_name"), pattern);
            params.insert(QStringLiteral("kw_alias"), pattern);
        } else {
            where += QStringLiteral(" AND f.title LIKE :kw_title ESCAPE '\\'");
            params.insert(QStringLiteral("kw_title"), pattern);
        }
    }

    // 创作日期区间（file.creation_date 固定 YYYY-MM-DD，字符串可直接比较，闭区间）
    const QString creationFrom = filters.creationFrom.trimmed();
    if (!creationFrom.isEmpty()) {
        where += QStringLiteral(" AND f.creation_date >= :creation_from");
        params.insert(QStringLiteral("creation_from"), creationFrom);
    }
    const QString creationTo = filters.creationTo.trimmed();
    if (!creationTo.isEmpty()) {
        where += QStringLiteral(" AND f.creation_date <= :creation_to");
        params.insert(QStringLiteral("creation_to"), creationTo);
    }

    // 入库时间区间（file.created_time 为 UTC ISO-8601）：UI 给的是本地日期，
    // 换算成 UTC 时刻后用半开区间 [from 当日 00:00, to 次日 00:00) 比较，
    // 避免「按本地日期筛却按 UTC 日期截断」造成的跨时区漏检。
    // 格式固定且补零，故字符串比较与时刻比较等价。
    const QDate createdFrom = QDate::fromString(filters.createdFrom.trimmed(),
                                                QStringLiteral("yyyy-MM-dd"));
    if (createdFrom.isValid()) {
        // startOfDay() 按本地时区解释（含 DST 正确处理），再换算 UTC
        const QString bound = createdFrom.startOfDay().toUTC().toString(Qt::ISODate);
        where += QStringLiteral(" AND f.created_time >= :created_from");
        params.insert(QStringLiteral("created_from"), bound);
    }
    const QDate createdTo = QDate::fromString(filters.createdTo.trimmed(),
                                              QStringLiteral("yyyy-MM-dd"));
    if (createdTo.isValid()) {
        // 上界取次日零时（半开区间），使「结束日当天入库」全部命中
        const QString bound = createdTo.addDays(1).startOfDay().toUTC().toString(Qt::ISODate);
        where += QStringLiteral(" AND f.created_time < :created_to");
        params.insert(QStringLiteral("created_to"), bound);
    }

    AppError local;
    DbAccess &db = m_library->dbAccess();

    // 总数（分页计数），失败即返回
    if (total != nullptr) {
        *total = db.scalar(
            QStringLiteral("SELECT COUNT(*) FROM file f JOIN type t ON t.id = f.type_id "
                           "WHERE %1").arg(where),
            params, &local).toInt();
        if (failed(local, error))
            return {};
    }

    // 当前页（offset/limit 同样参数化）
    params.insert(QStringLiteral("limit"), qMax(0, limit));
    params.insert(QStringLiteral("offset"), qMax(0, offset));
    const QList<QVariantMap> rows = db.select(
        QStringLiteral("SELECT f.id, f.title, f.score, f.size, f.storage_path, f.aspect_ratio, "
                       "f.creation_date, f.created_time, f.updated_time, t.form AS type_form "
                       "FROM file f JOIN type t ON t.id = f.type_id WHERE %1 "
                       "ORDER BY f.id LIMIT :limit OFFSET :offset").arg(where),
        params, &local);
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
    }

    // 创作者多对多（全局关联表，不依赖分表）：按 sort_order 列出全部名字。
    // creatorName=主要创作者（无关联则兜底「佚名」）；creators=完整列表；creatorCount=数量。
    const QList<QVariantMap> creatorRows = db.select(
        QStringLiteral("SELECT c.name FROM file_creator fc "
                       "JOIN creator c ON c.id = fc.creator_id "
                       "WHERE fc.file_id = :id ORDER BY fc.sort_order, fc.creator_id"),
        {{QStringLiteral("id"), id}}, &local);
    if (failed(local, error))
        return {};
    QStringList creators;
    for (const QVariantMap &creatorRow : creatorRows)
        creators.append(creatorRow.value(QStringLiteral("name")).toString());
    row.insert(QStringLiteral("creators"), creators);
    row.insert(QStringLiteral("creatorName"),
               creators.isEmpty() ? CreatorService::anonymousName() : creators.first());
    row.insert(QStringLiteral("creatorCount"), creators.size());
    // 封面常驻 envs/<库>/cover/<桶>/<id>.jpg（离线也可显示）；缺失时置空 URL。
    // 预览面板 / 详情窗口直接消费此字段，与列表行 enrichRows 的 coverUrl 同款。
    const QString libraryName = m_library->currentName();
    const int bucket = row.value(QStringLiteral("storage_path")).toInt();
    const QString coverAbs =
        StorageService::coverPath(m_paths->coverDir(libraryName), bucket, id);
    row.insert(QStringLiteral("coverUrl"),
               QFileInfo::exists(coverAbs) ? QUrl::fromLocalFile(coverAbs) : QUrl());
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

bool PackageService::updateCreationDate(qint64 id, const QString &date, AppError *error)
{
    const QString trimmed = date.trimmed();
    // 必填且精度固定：空串/非法格式一律拒绝（与导入侧同一套校验）
    if (!StorageService::isValidCreationDate(trimmed)) {
        setOut(error, AppError::Validation,
               trimmed.isEmpty() ? QStringLiteral("创作日期不能为空")
                                 : QStringLiteral("创作日期格式须为 YYYY-MM-DD：%1").arg(trimmed));
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

    if (!db.execute(
            QStringLiteral("UPDATE file SET creation_date = :date, updated_time = :now WHERE id = :id"),
            {{QStringLiteral("date"), trimmed},
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
