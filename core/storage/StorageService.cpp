#include "StorageService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDebug>

#include "common/Timestamps.h"
#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"

namespace {

constexpr qint64 kPackagesPerBucket = 5000;

// 递归复制目录（导出保留子目录结构用）
bool copyDirectoryRecursively(const QString &srcPath, const QString &destPath)
{
    QDir src(srcPath);
    if (!src.exists())
        return false;
    QDir dest(destPath);
    if (!dest.exists() && !dest.mkpath(QStringLiteral(".")))
        return false;

    for (const QFileInfo &info :
         src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString target = dest.filePath(info.fileName());
        if (info.isDir()) {
            if (!copyDirectoryRecursively(info.absoluteFilePath(), target))
                return false;
        } else if (!QFile::copy(info.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool StorageService::isValidCreationDate(const QString &date)
{
    const QString trimmed = date.trimmed();
    if (trimmed.isEmpty())
        return true; // 未知日期合法，落库为 NULL

    const QStringList parts = trimmed.split(QLatin1Char('-'));
    if (parts.size() < 1 || parts.size() > 3)
        return false;
    for (int i = 0; i < parts.size(); ++i) {
        // 年 4 位；月、日 2 位
        if (parts.at(i).size() != (i == 0 ? 4 : 2))
            return false;
        bool ok = false;
        parts.at(i).toInt(&ok);
        if (!ok)
            return false;
    }
    const int year = parts.at(0).toInt();
    if (year < 0 || year > 9999)
        return false;
    if (parts.size() >= 2) {
        const int month = parts.at(1).toInt();
        if (month < 1 || month > 12)
            return false;
    }
    if (parts.size() == 3) {
        const int day = parts.at(2).toInt();
        if (day < 1 || day > 31)
            return false;
    }
    return true;
}

StorageService::StorageService(AppPaths *paths, LibraryService *library, CreatorService *creators,
                               EventBus *events)
    : m_paths(paths)
    , m_library(library)
    , m_creators(creators)
    , m_events(events)
{
}

int StorageService::bucketOf(qint64 id)
{
    return static_cast<int>((id - 1) / kPackagesPerBucket) + 1;
}

QString StorageService::indexedName(int index)
{
    return QStringLiteral("%1").arg(index, 3, 10, QLatin1Char('0'));
}

bool StorageService::isSafeFormName(const QString &form)
{
    if (form.isEmpty() || form.size() > 64)
        return false;
    if (!form.at(0).isLower() || !form.at(0).isLetter())
        return false;
    for (const QChar ch : form) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
            return false;
        if (ch.isLetter() && !ch.isLower())
            return false;
    }
    return true;
}

QString StorageService::packageDir(const QString &linkAddress, int bucket, qint64 id)
{
    return linkAddress + QLatin1Char('/') + QString::number(bucket) + QLatin1Char('/')
           + QString::number(id);
}

QString StorageService::coverPath(const QString &coverRoot, int bucket, qint64 id)
{
    return coverRoot + QLatin1Char('/') + QString::number(bucket) + QLatin1Char('/')
           + QString::number(id) + QStringLiteral(".jpg");
}

qint64 StorageService::importPackage(const ImportPlan &plan, AppError *error)
{
    auto fail = [&](AppError::Code code, const QString &message) -> qint64 {
        if (error != nullptr)
            *error = AppError::fail(code, message);
        return -1;
    };

    // ---- 1. 前置校验（开发文档 5.4 ⑤）----
    const QString libraryName = m_library->currentName();
    if (libraryName.isEmpty())
        return fail(AppError::Validation, QStringLiteral("没有打开的库，无法导入"));

    const LibraryEntry entry = m_library->config().entry(libraryName);
    if (entry.linkAddress.isEmpty())
        return fail(AppError::Config, QStringLiteral("当前库缺少链接库地址"));

    if (!isSafeFormName(plan.form))
        return fail(AppError::Validation, QStringLiteral("非法类型标识：%1").arg(plan.form));

    if (plan.files.isEmpty())
        return fail(AppError::Validation, QStringLiteral("导入计划没有源文件"));

    for (const QString &file : plan.files) {
        if (!QFileInfo(file).isFile())
            return fail(AppError::Io, QStringLiteral("源文件不存在或不是文件：%1").arg(file));
    }

    QString coverSource = plan.coverSourcePath.trimmed();
    if (coverSource.isEmpty())
        coverSource = plan.files.first();
    else if (!QFileInfo(coverSource).isFile())
        return fail(AppError::Io, QStringLiteral("封面源文件不存在：%1").arg(coverSource));

    const QString creationDate = plan.creationDate.trimmed();
    if (!creationDate.isEmpty() && !isValidCreationDate(creationDate))
        return fail(AppError::Validation,
                    QStringLiteral("创作日期格式须为 YYYY / YYYY-MM / YYYY-MM-DD：%1").arg(creationDate));

    const QString title = plan.title.trimmed().isEmpty() ? QStringLiteral("untitled")
                                                         : plan.title.trimmed();

    DbAccess &db = m_library->dbAccess();

    // 离线（移动盘/NAS 未挂载）时禁止导入（开发文档 4.5）
    if (!QDir(entry.linkAddress).exists())
        return fail(AppError::Io,
                    QStringLiteral("实际库目录不可达，离线状态不能导入：%1").arg(entry.linkAddress));

    // 类型须已在本库启用（type 表登记）
    AppError typeError;
    QVariantMap typeRow;
    const bool typeFound = db.selectOne(QStringLiteral("SELECT id FROM type WHERE form = :form"),
                                        {{QStringLiteral("form"), plan.form}}, typeRow,
                                        &typeError);
    if (!typeError.ok()) {
        if (error != nullptr) *error = typeError;
        return -1;
    }
    if (!typeFound)
        return fail(AppError::Conflict, QStringLiteral("类型未在该库启用：%1").arg(plan.form));
    const qint64 typeId = typeRow.value(QStringLiteral("id")).toLongLong();

    // 分表实际列（表名来自 type 表登记，仍以安全标识符校验兜底）
    AppError pragmaError;
    const QList<QVariantMap> columns = db.select(
        QStringLiteral("PRAGMA table_info(%1)").arg(plan.form), {}, &pragmaError);
    if (!pragmaError.ok()) {
        if (error != nullptr) *error = pragmaError;
        return -1;
    }
    bool hasCreatorColumn = false;
    bool hasDateColumn = false;
    for (const QVariantMap &column : columns) {
        const QString name = column.value(QStringLiteral("name")).toString();
        if (name == QStringLiteral("creator_id"))
            hasCreatorColumn = true;
        else if (name == QStringLiteral("creation_date"))
            hasDateColumn = true;
    }
    if (!plan.creatorName.trimmed().isEmpty() && !hasCreatorColumn)
        return fail(AppError::Conflict,
                    QStringLiteral("类型 %1 的分表不支持创作者字段").arg(plan.form));
    if (!creationDate.isEmpty() && !hasDateColumn)
        return fail(AppError::Conflict,
                    QStringLiteral("类型 %1 的分表不支持创作日期字段").arg(plan.form));

    // ---- 2. 开事务：创作者解析与占位行随事务回滚 ----
    if (!db.transaction(error))
        return -1;

    qint64 creatorId = 0;
    if (hasCreatorColumn && !plan.creatorName.trimmed().isEmpty()) {
        creatorId = m_creators->resolveOrCreate(plan.creatorName, CreatorService::Person, error);
        if (creatorId < 0) {
            db.rollback();
            return -1;
        }
    }

    // 分配 id：先写占位行取得自增 id，桶号随之确定（提交前回填）
    const QString now = isoNowUtc();
    qint64 id = 0;
    if (!db.execute(QStringLiteral(
                        "INSERT INTO file (title, type_id, score, size, storage_path, "
                        "created_time, updated_time) "
                        "VALUES (:title, :type_id, 0, 0, 0, :now, :now)"),
                    {{QStringLiteral("title"), title},
                     {QStringLiteral("type_id"), typeId},
                     {QStringLiteral("now"), now}},
                    &id, error)) {
        db.rollback();
        return -1;
    }
    const int bucket = bucketOf(id);

    const QString targetDir = packageDir(entry.linkAddress, bucket, id);
    const QString targetCover = coverPath(m_paths->coverDir(libraryName), bucket, id);

    // 失败善后：回滚 + 清除本次新建的包文件夹与封面（开发文档 5.4）
    auto abortImport = [&](const QString &context) -> qint64 {
        db.rollback();
        QDir(targetDir).removeRecursively();
        QFile::remove(targetCover);
        if (error != nullptr && error->ok())
            *error = AppError::fail(AppError::Io, context);
        else if (error != nullptr)
            error->message = context + QStringLiteral("：") + error->message;
        return -1;
    };

    // ---- 3. 复制入桶（复制式导入，绝不移动/删除源文件；重命名为包内序号）----
    if (!QDir().mkpath(targetDir))
        return abortImport(QStringLiteral("包目录创建失败：%1").arg(targetDir));

    qint64 totalSize = 0;
    for (int index = 0; index < plan.files.size(); ++index) {
        const QFileInfo sourceInfo(plan.files.at(index));
        const QString suffix = sourceInfo.suffix();
        const QString targetFile = targetDir + QLatin1Char('/') + indexedName(index + 1)
                                   + (suffix.isEmpty() ? QString()
                                                       : QLatin1Char('.') + suffix);
        if (!QFile::copy(sourceInfo.absoluteFilePath(), targetFile)) {
            if (!QFileInfo::exists(targetFile))
                return abortImport(QStringLiteral("文件复制失败：%1 → %2")
                                       .arg(sourceInfo.absoluteFilePath(), targetFile));
        }
        totalSize += QFileInfo(targetFile).size();
    }

    // ---- 4. 封面（核心统一生成，长边 m_coverLongEdge）----
    if (!generateCover(coverSource, targetCover, error))
        return abortImport(QStringLiteral("封面生成失败"));

    // ---- 5. 写分表（列按分表实际结构裁剪）----
    if (hasCreatorColumn || hasDateColumn) {
        QStringList columnNames{QStringLiteral("id")};
        QStringList valueNames{QStringLiteral(":id")};
        QVariantMap params{{QStringLiteral("id"), id}};
        if (hasCreatorColumn) {
            columnNames.append(QStringLiteral("creator_id"));
            valueNames.append(QStringLiteral(":creator_id"));
            params.insert(QStringLiteral("creator_id"), creatorId);
        }
        if (hasDateColumn) {
            columnNames.append(QStringLiteral("creation_date"));
            valueNames.append(QStringLiteral(":creation_date"));
            // 空日期存 NULL（未知）
            params.insert(QStringLiteral("creation_date"),
                          creationDate.isEmpty() ? QVariant() : QVariant(creationDate));
        }
        if (!db.execute(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                            .arg(plan.form, columnNames.join(QLatin1Char(',')),
                                 valueNames.join(QLatin1Char(','))),
                        params, error))
            return abortImport(QStringLiteral("分表写入失败"));
    }

    // ---- 6. 回填 size 与桶号 ----
    if (!db.execute(QStringLiteral("UPDATE file SET size = :size, storage_path = :bucket WHERE id = :id"),
                    {{QStringLiteral("size"), totalSize},
                     {QStringLiteral("bucket"), bucket},
                     {QStringLiteral("id"), id}},
                    error))
        return abortImport(QStringLiteral("包信息回填失败"));

    // ---- 7. 提交并广播 ----
    if (!db.commit(error)) {
        db.rollback();
        QDir(targetDir).removeRecursively();
        QFile::remove(targetCover);
        return -1;
    }

    emit m_events->packageCreated(id, plan.form);
    qInfo().noquote() << "[storage] 已导入包: id =" << id << "桶 =" << bucket << "标题 ="
                      << title << "文件数 =" << plan.files.size() << "大小 =" << totalSize;
    return id;
}

bool StorageService::isLinkOnline() const
{
    const QString name = m_library->currentName();
    if (name.isEmpty())
        return false;
    return QDir(m_library->config().entry(name).linkAddress).exists();
}

bool StorageService::regenerateCover(const QString &linkAddress, int bucket, qint64 id,
                                     AppError *error)
{
    auto fail = [&](AppError::Code code, const QString &message) -> bool {
        if (error != nullptr)
            *error = AppError::fail(code, message);
        return false;
    };

    const QString sourceDir = packageDir(linkAddress, bucket, id);
    if (!QDir(sourceDir).exists())
        return fail(AppError::Io, QStringLiteral("包目录缺失：%1").arg(sourceDir));

    // 取包内第一个图片文件为封面源（与导入默认行为一致）
    const QStringList images = QDir(sourceDir).entryList(
        {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
         QStringLiteral("*.gif"), QStringLiteral("*.bmp"), QStringLiteral("*.webp")},
        QDir::Files | QDir::Readable, QDir::Name);
    if (images.isEmpty())
        return fail(AppError::Io,
                    QStringLiteral("包内无图片文件，无法生成封面：%1").arg(sourceDir));

    const QString libraryName = m_library->currentName();
    if (libraryName.isEmpty())
        return fail(AppError::Validation, QStringLiteral("没有打开的库"));

    const QString targetCover = coverPath(m_paths->coverDir(libraryName), bucket, id);
    if (!generateCover(sourceDir + QLatin1Char('/') + images.first(), targetCover, error))
        return false;

    qInfo().noquote() << "[storage] 已重新生成封面: id =" << id << "桶 =" << bucket;
    return true;
}

int StorageService::exportPackage(qint64 id, const QString &destDir, AppError *error)
{
    auto fail = [&](AppError::Code code, const QString &message) -> int {
        if (error != nullptr)
            *error = AppError::fail(code, message);
        return -1;
    };

    const QString libraryName = m_library->currentName();
    if (libraryName.isEmpty())
        return fail(AppError::Validation, QStringLiteral("没有打开的库"));
    if (!isLinkOnline())
        return fail(AppError::Io, QStringLiteral("实际库离线，无法导出"));

    const QString linkAddress = m_library->config().entry(libraryName).linkAddress;

    AppError local;
    QVariantMap row;
    DbAccess &db = m_library->dbAccess();
    if (!db.selectOne(QStringLiteral("SELECT storage_path FROM file WHERE id = :id"),
                      {{QStringLiteral("id"), id}}, row, &local)) {
        // 区分两种失败：查询真错误（local 非 ok）转发原错误；无结果走下方"数据包不存在"
        if (!local.ok()) {
            if (error != nullptr) *error = local;
            return -1;
        }
    }
    if (row.isEmpty())
        return fail(AppError::Validation, QStringLiteral("数据包不存在：#%1").arg(id));

    const int bucket = row.value(QStringLiteral("storage_path")).toInt();
    const QString sourceDir = packageDir(linkAddress, bucket, id);
    if (!QDir(sourceDir).exists())
        return fail(AppError::Io, QStringLiteral("包目录缺失：%1").arg(sourceDir));

    QDir dest(destDir);
    if (!dest.exists())
        return fail(AppError::Validation,
                    QStringLiteral("导出目标目录不存在：%1").arg(QDir::toNativeSeparators(destDir)));
    // 防误覆盖：目标目录必须为空
    if (!dest.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        return fail(AppError::Conflict,
                    QStringLiteral("导出目标目录非空，请选择空目录：%1")
                        .arg(QDir::toNativeSeparators(destDir)));

    // 原样复制包内全部条目（draw 为扁平 001.ext；保留子目录以兼容未来类型）
    int copied = 0;
    QStringList created; // 本次已复制条目，失败时回滚清理
    const QDir src(sourceDir);
    for (const QFileInfo &info :
         src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString target = dest.filePath(info.fileName());
        bool ok = false;
        if (info.isDir())
            ok = copyDirectoryRecursively(info.absoluteFilePath(), target);
        else
            ok = QFile::copy(info.absoluteFilePath(), target);
        if (!ok) {
            // 失败回滚：删除本次已复制条目，避免残留半成品导致目标非空、重试死锁
            for (const QString &made : created) {
                const QFileInfo madeInfo(made);
                if (madeInfo.isDir())
                    QDir(made).removeRecursively();
                else
                    QFile::remove(made);
            }
            return fail(AppError::Io,
                        QStringLiteral("导出复制失败：%1").arg(info.absoluteFilePath()));
        }
        created.append(target);
        ++copied;
    }

    qInfo().noquote() << "[storage] 已导出包: id =" << id << "→" << destDir << "文件数 =" << copied;
    return copied;
}

bool StorageService::generateCover(const QString &sourcePath, const QString &targetPath,
                                   AppError *error)
{
    QImage image(sourcePath);
    if (image.isNull()) {
        if (error != nullptr) {
            *error = AppError::fail(AppError::Io,
                                    QStringLiteral("封面源文件无法作为图片读取：%1").arg(sourcePath));
        }
        return false;
    }

    // 仅缩小不放大：长边超过上限时等比缩到上限（开发文档 5.3）
    const int longEdge = qMax(image.width(), image.height());
    if (longEdge > m_coverLongEdge) {
        const qreal ratio = static_cast<qreal>(m_coverLongEdge) / longEdge;
        const int targetWidth = qMax(1, qRound(image.width() * ratio));
        const int targetHeight = qMax(1, qRound(image.height() * ratio));
        image = image.scaled(targetWidth, targetHeight, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }

    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Io,
                                    QStringLiteral("封面目录创建失败：%1").arg(targetPath));
        return false;
    }
    if (!image.save(targetPath, "JPEG", 85)) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Io, QStringLiteral("封面写入失败：%1").arg(targetPath));
        return false;
    }
    return true;
}
