#include "StorageService.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QDebug>
#include <cmath>

#include "common/Timestamps.h"
#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"

namespace {

constexpr qint64 kPackagesPerBucket = 5000;

// 封面清晰度上限（固定值）：图片对角线像素。数据类型多、封面尺寸多样，
// 用「长边」无法统一描述清晰度，暂按对角线归一，超过则等比缩小（仅缩小不放大）。
constexpr int kCoverDiagonal = 512;

// 读图片文件头得到宽高比（w/h，只解析文件头、不解码全图）；不可读返回 0 表示未知
qreal readAspectRatio(const QString &imagePath)
{
    QImageReader reader(imagePath);
    const QSize size = reader.size();
    if (!size.isValid() || size.height() <= 0)
        return 0.0;
    return static_cast<qreal>(size.width()) / static_cast<qreal>(size.height());
}

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
    // 精度固定：只接受 YYYY-MM-DD（拒绝 2021 / 2021-06 这类粗精度，也不再容忍空串）
    return QDate::fromString(date.trimmed(), QStringLiteral("yyyy-MM-dd")).isValid();
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

QString StorageService::packageFilePath(const QString &linkAddress, int bucket, qint64 id,
                                        const QString &relativePath)
{
    if (relativePath.isEmpty())
        return QString();
    return packageDir(linkAddress, bucket, id) + QLatin1Char('/') + relativePath;
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

    // 创作日期（v5 起为核心字段 file.creation_date）：格式固定 YYYY-MM-DD，导入必填；
    // 向导已给默认值（当天），此处兜底，保证导入侧永不写空值
    QString creationDate = plan.creationDate.trimmed();
    if (creationDate.isEmpty())
        creationDate = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    else if (!isValidCreationDate(creationDate))
        return fail(AppError::Validation,
                    QStringLiteral("创作日期格式须为 YYYY-MM-DD：%1").arg(creationDate));

    const QString title = plan.title.trimmed().isEmpty() ? QStringLiteral("untitled")
                                                         : plan.title.trimmed();

    // 封面长宽比（瀑布流布局用）：封面为等比缩放，其 AR 等于原图 AR；读文件头即可
    const qreal coverAspect = readAspectRatio(coverSource);

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

    // ---- 2. 开事务：创作者解析与占位行随事务回滚 ----
    if (!db.transaction(error))
        return -1;

    // 多创作者：拆分输入文本并逐个 find-or-create（顺序即 sort_order，第一个为主要创作者）；
    // 空输入 → 归为默认创作者「佚名」（不产生空关联，保证始终可检索）
    QList<qint64> creatorIds;
    const QStringList creatorNames = CreatorService::splitNames(plan.creatorName);
    if (creatorNames.isEmpty()) {
        const qint64 creatorId = m_creators->resolveOrCreate(QString(), error);
        if (creatorId < 0) {
            db.rollback();
            return -1;
        }
        creatorIds.append(creatorId);
    } else {
        for (const QString &name : creatorNames) {
            const qint64 creatorId = m_creators->resolveOrCreate(name, error);
            if (creatorId < 0) {
                db.rollback();
                return -1;
            }
            creatorIds.append(creatorId);
        }
    }

    // 分配 id：先写占位行取得自增 id，桶号随之确定（提交前回填）
    const QString now = isoNowUtc();
    qint64 id = 0;
    if (!db.execute(QStringLiteral(
                        "INSERT INTO file (title, type_id, score, size, storage_path, aspect_ratio, "
                        "creation_date, created_time, updated_time) "
                        "VALUES (:title, :type_id, :score, 0, 0, :aspect, :creation_date, :now, :now)"),
                    {{QStringLiteral("title"), title},
                     {QStringLiteral("type_id"), typeId},
                     {QStringLiteral("score"), plan.score},
                     {QStringLiteral("aspect"), coverAspect},
                     {QStringLiteral("creation_date"), creationDate},
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

    // ---- 4. 封面（核心统一生成，清晰度固定为对角线 kCoverDiagonal）----
    if (!generateCover(coverSource, targetCover, error))
        return abortImport(QStringLiteral("封面生成失败"));

    // ---- 5. 写创作者关联（多对多；空输入已归为「佚名」，故至少一条）----
    for (int index = 0; index < creatorIds.size(); ++index) {
        if (!db.execute(QStringLiteral(
                            "INSERT INTO file_creator (file_id, creator_id, sort_order) "
                            "VALUES (:file_id, :creator_id, :sort_order)"),
                        {{QStringLiteral("file_id"), id},
                         {QStringLiteral("creator_id"), creatorIds.at(index)},
                         {QStringLiteral("sort_order"), index}},
                        error))
            return abortImport(QStringLiteral("创作者关联写入失败"));
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

int StorageService::backfillAspectRatios(AppError *error)
{
    const QString libraryName = m_library->currentName();
    if (libraryName.isEmpty())
        return 0;
    DbAccess &db = m_library->dbAccess();

    // 只处理 AR 未知（<=0）的行；封面缺失时以 1.0 兜底，避免每次打开重复扫描
    AppError queryError;
    const QList<QVariantMap> rows = db.select(
        QStringLiteral("SELECT id, storage_path FROM file WHERE aspect_ratio <= 0"), {},
        &queryError);
    if (!queryError.ok()) {
        if (error != nullptr)
            *error = queryError;
        return -1;
    }
    if (rows.isEmpty())
        return 0;

    const QString coverRoot = m_paths->coverDir(libraryName);
    if (!db.transaction(error))
        return -1;

    int updated = 0;
    for (const QVariantMap &row : rows) {
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        const int bucket = row.value(QStringLiteral("storage_path")).toInt();
        const QString coverAbs = coverPath(coverRoot, bucket, id);
        qreal ar = QFileInfo::exists(coverAbs) ? readAspectRatio(coverAbs) : 0.0;
        if (ar <= 0.0)
            ar = 1.0; // 未知兜底为方形
        if (!db.execute(QStringLiteral("UPDATE file SET aspect_ratio = :ar WHERE id = :id"),
                        {{QStringLiteral("ar"), ar}, {QStringLiteral("id"), id}}, error)) {
            db.rollback();
            return -1;
        }
        ++updated;
    }
    if (!db.commit(error)) {
        db.rollback();
        return -1;
    }
    return updated;
}

bool StorageService::isLinkOnline() const
{
    const QString name = m_library->currentName();
    if (name.isEmpty())
        return false;
    return QDir(m_library->config().entry(name).linkAddress).exists();
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

    // 仅缩小不放大：对角线超过上限时等比缩到上限（开发文档 5.3）
    const qreal diagonal = std::hypot(static_cast<qreal>(image.width()),
                                      static_cast<qreal>(image.height()));
    if (diagonal > kCoverDiagonal) {
        const qreal ratio = static_cast<qreal>(kCoverDiagonal) / diagonal;
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
