#include "ImportService.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

#include "common/MediaTypes.h"
#include "dedup/ContentHasher.h"
#include "dedup/DedupService.h"
#include "library/LibraryService.h"
#include "storage/StorageService.h"
#include "text/BookSplitter.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

namespace {

// 预处理扫描读取目录级 meta.json（draw 与 novel 共用；缺失/损坏仅忽略元数据）
QVariantMap readMetaFile(const QString &dirPath)
{
    QVariantMap meta;
    QFile file(dirPath + QStringLiteral("/meta.json"));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return meta;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject())
        meta = doc.object().toVariantMap();
    return meta;
}

// 从 meta 取标题/创作者/日期/封面路径，README 见开发文档 5.5
QVariantMap planMeta(const QVariantMap &meta, const QString &fallbackTitle,
                     const QString &dirPath, QStringList *warnings)
{
    QVariantMap out{
        {QStringLiteral("title"), meta.value(QStringLiteral("title")).toString().isEmpty()
                                     ? fallbackTitle
                                     : meta.value(QStringLiteral("title")).toString()},
        {QStringLiteral("creatorName"), meta.value(QStringLiteral("creator")).toString()},
        {QStringLiteral("creationDate"), meta.value(QStringLiteral("creation_date")).toString()},
    };

    const QString coverName = meta.value(QStringLiteral("cover")).toString();
    if (!coverName.isEmpty()) {
        const QString coverPath = dirPath + QLatin1Char('/') + coverName;
        if (QFileInfo::exists(coverPath))
            out.insert(QStringLiteral("coverSourcePath"), coverPath);
        else if (warnings != nullptr)
            warnings->append(QStringLiteral("meta.json 指定的封面不存在：%1").arg(coverName));
    }
    return out;
}

// 章节预览分析的单次预算：批量扫描时避免把几百本大书全部读一遍造成长时间卡顿
constexpr qint64 kPreviewBudgetBytes = 64LL * 1024 * 1024;

// 判断文件的媒体类别是否被类型声明接受（扫描与导入校验共用）
// 输入 package 为类型包，path 为文件路径；输出 true 表示 image/video 在 media 声明内
bool packageAcceptsPath(const TypePackage &package, const QString &path)
{
    const QString kind = MediaTypes::mediaKind(path);
    return !kind.isEmpty() && package.media.contains(kind);
}

} // namespace

ImportService::ImportService(LibraryService *library, StorageService *storage,
                             TypePackageManager *typePackages, DedupService *dedup,
                             QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_storage(storage)
    , m_typePackages(typePackages)
    , m_dedup(dedup)
{
}

ImportPlan ImportService::planFromMap(const QString &form, const TypePackage *package,
                                      const QVariantMap &map)
{
    ImportPlan plan;
    plan.form = form;
    plan.title = map.value(QStringLiteral("title")).toString();
    plan.creatorName = map.value(QStringLiteral("creatorName")).toString();
    plan.creationDate = map.value(QStringLiteral("creationDate")).toString();
    plan.score = map.value(QStringLiteral("score"), 0).toInt();
    plan.coverSourcePath = map.value(QStringLiteral("coverSourcePath")).toString();

    // 单媒体包保留原文件名（一个视频一个包，导出/续看都按原名更直观）；
    // 序列布局仍按零填充序号命名，保证包内顺序稳定
    const bool keepOriginalName =
        package != nullptr && package->packageLayoutKind == QStringLiteral("singleMedia");

    const QVariantList entries = map.value(QStringLiteral("entries")).toList();
    if (!entries.isEmpty()) {
        // 显式条目：相对路径由计划产出方决定（小说章文件等）
        for (const QVariant &value : entries) {
            const QVariantMap entry = value.toMap();
            ImportEntry item;
            item.sourcePath = entry.value(QStringLiteral("source")).toString();
            item.relativePath = entry.value(QStringLiteral("relativePath")).toString();
            plan.entries.append(item);
        }
    } else {
        // 扁平清单简写（imageSequence / singleMedia / mediaSequence）
        const QVariantList files = map.value(QStringLiteral("files")).toList();
        for (int index = 0; index < files.size(); ++index) {
            const QFileInfo info(files.at(index).toString());
            const QString suffix = info.suffix();
            ImportEntry item;
            item.sourcePath = info.absoluteFilePath();
            item.relativePath = keepOriginalName
                ? info.fileName()
                : StorageService::indexedName(index + 1)
                      + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);
            plan.entries.append(item);
        }
    }

    // 封面缺省取首个图片条目；纯文本/纯视频包自然为空
    if (plan.coverSourcePath.isEmpty()) {
        for (const ImportEntry &entry : plan.entries) {
            if (MediaTypes::isImageFile(entry.sourcePath)) {
                plan.coverSourcePath = entry.sourcePath;
                break;
            }
        }
    }

    // 无图片可作封面且类型接受视频 → 由核心生成占位封面（深灰底播放图标）
    if (plan.coverSourcePath.isEmpty() && package != nullptr
        && package->media.contains(QStringLiteral("video"))) {
        plan.placeholderCover = true;
    }
    return plan;
}

QVariantMap ImportService::executePlans(const QString &form, const QVariantList &plans)
{
    const TypePackage *package = m_typePackages->package(form);
    const QString kind = package != nullptr ? package->packageLayoutKind : QString();

    QVariantList results;
    QVariantList importedIds;
    int success = 0;
    int failed = 0;

    for (int index = 0; index < plans.size(); ++index) {
        const QVariantMap map = plans.at(index).toMap();
        QVariantMap item;
        if (kind == QStringLiteral("wholeBookText"))
            item = executeBookPlan(form, map, index);
        else
            item = executeMediaPlan(form, map, index); // imageSequence / singleMedia / mediaSequence

        if (item.value(QStringLiteral("ok")).toBool()) {
            ++success;
            importedIds.append(item.value(QStringLiteral("id")));
        } else {
            ++failed;
        }
        results.append(item);
    }

    qInfo().noquote() << "[import] 批量执行完成：共" << plans.size() << "成功" << success
                      << "失败" << failed;
    return {
        {QStringLiteral("ok"), failed == 0},
        {QStringLiteral("total"), plans.size()},
        {QStringLiteral("success"), success},
        {QStringLiteral("failed"), failed},
        {QStringLiteral("results"), results},
        {QStringLiteral("importedIds"), importedIds},
    };
}

QVariantMap ImportService::executeMediaPlan(const QString &form, const QVariantMap &map, int index)
{
    QVariantMap item{{QStringLiteral("index"), index}};

    const TypePackage *package = m_typePackages->package(form);
    if (package == nullptr) {
        item.insert(QStringLiteral("ok"), false);
        item.insert(QStringLiteral("message"), QStringLiteral("类型包未安装：%1").arg(form));
        return item;
    }

    const ImportPlan plan = planFromMap(form, package, map);

    // 媒体类别校验：扩展名必须落在类型声明的 media 内（防止把图片塞进视频类型，反之亦然）
    for (const ImportEntry &entry : plan.entries) {
        if (packageAcceptsPath(*package, entry.sourcePath))
            continue;
        item.insert(QStringLiteral("ok"), false);
        item.insert(QStringLiteral("message"),
                    QStringLiteral("文件类型不受该类型支持：%1")
                        .arg(QFileInfo(entry.sourcePath).fileName()));
        return item;
    }

    // 单媒体包：每包恰好 1 个文件（向导层已拦截多选，此处为核心防御性校验）
    if (package->packageLayoutKind == QStringLiteral("singleMedia") && plan.entries.size() != 1) {
        item.insert(QStringLiteral("ok"), false);
        item.insert(QStringLiteral("message"), QStringLiteral("该类型每包仅允许 1 个文件"));
        return item;
    }

    // 写入前完全重复预检：命中即阻断，不复制任何文件（视频/文本不参与，预检内部跳过）
    QVariantMap precheck{{QStringLiteral("ok"), true},
                         {QStringLiteral("hasDuplicates"), false},
                         {QStringLiteral("files"), QVariantList{}}};
    if (m_dedup != nullptr) {
        QVariantList paths;
        for (const ImportEntry &entry : plan.entries)
            paths.append(entry.sourcePath);
        precheck = m_dedup->precheck(paths);
        if (!precheck.value(QStringLiteral("ok")).toBool()) {
            item.insert(QStringLiteral("ok"), false);
            item.insert(QStringLiteral("message"),
                        precheck.value(QStringLiteral("message")).toString());
            return item;
        }
        if (precheck.value(QStringLiteral("hasDuplicates")).toBool()) {
            const QVariantList files = precheck.value(QStringLiteral("files")).toList();
            QVariantList duplicates;
            for (const QVariant &value : files) {
                const QVariantMap file = value.toMap();
                if (file.value(QStringLiteral("duplicate")).toBool())
                    duplicates.append(file);
            }
            item.insert(QStringLiteral("ok"), false);
            item.insert(QStringLiteral("duplicate"), true);
            item.insert(QStringLiteral("duplicates"), duplicates);
            item.insert(QStringLiteral("message"),
                        QStringLiteral("检测到 %1 个文件与库中已有内容完全重复，已阻断导入")
                            .arg(duplicates.size()));
            qInfo().noquote() << "[import] 完全重复，已阻断：" << plan.title
                              << "重复文件数 =" << duplicates.size();
            return item;
        }
    }

    AppError error;
    const qint64 id = m_storage->importPackage(plan, &error);
    if (id <= 0) {
        item.insert(QStringLiteral("ok"), false);
        item.insert(QStringLiteral("message"), error.message);
        return item;
    }

    item.insert(QStringLiteral("ok"), true);
    item.insert(QStringLiteral("id"), id);
    // 指纹写入失败不影响导入结果，后续可由「补指纹」入口修复；
    // 预检已算出的哈希会随 prechecked 传入，避免二次读盘
    indexImportedFiles(id, plan.entries, precheck.value(QStringLiteral("files")).toList());
    return item;
}

QVariantMap ImportService::executeBookPlan(const QString &form, const QVariantMap &map, int index)
{
    QVariantMap item{{QStringLiteral("index"), index}, {QStringLiteral("ok"), false}};

    auto reject = [&item](const QString &message) {
        item.insert(QStringLiteral("message"), message);
        return item;
    };

    const QString sourcePath = map.value(QStringLiteral("sourcePath")).toString().trimmed();
    if (sourcePath.isEmpty())
        return reject(QStringLiteral("未选择小说文件"));

    const TypePackage *package = m_typePackages->package(form);
    if (package == nullptr)
        return reject(QStringLiteral("类型包未安装：%1").arg(form));

    const QVariantList rules = package->packageLayout.value(QStringLiteral("chapterRules")).toList();
    const BookAnalysis analysis = BookSplitter::analyze(sourcePath, rules);
    if (!analysis.ok)
        return reject(analysis.error);

    // 切分产物先写临时目录，再由 StorageService 按条目复制入桶；无论成败都清理
    const QString staging = QDir::tempPath() + QStringLiteral("/jarvis-book-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(staging))
        return reject(QStringLiteral("无法创建导入临时目录"));

    BookSourceInfo source;
    source.path = sourcePath;
    source.name = QFileInfo(sourcePath).fileName();
    AppError hashError;
    source.sha256 = ContentHasher::sha256File(sourcePath, nullptr, &hashError);
    source.encoding = analysis.encoding;

    QString writeError;
    if (!BookSplitter::writePackage(source, analysis, staging, &writeError)) {
        QDir(staging).removeRecursively();
        return reject(writeError);
    }

    ImportPlan plan = planFromMap(form, package, map);
    plan.entries.clear();
    for (const BookChapter &chapter : analysis.chapters) {
        const QString fileName = QString::number(chapter.index) + QStringLiteral(".txt");
        ImportEntry entry;
        entry.sourcePath = staging + QLatin1Char('/') + fileName;
        entry.relativePath = fileName;
        plan.entries.append(entry);
    }
    plan.entries.append(ImportEntry{staging + QStringLiteral("/config.txt"),
                                    QStringLiteral("config.txt")});

    if (plan.title.trimmed().isEmpty())
        plan.title = QFileInfo(sourcePath).completeBaseName();

    AppError error;
    const qint64 id = m_storage->importPackage(plan, &error);
    QDir(staging).removeRecursively();

    if (id <= 0)
        return reject(error.message);

    item.insert(QStringLiteral("ok"), true);
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("chapters"), analysis.chapters.size());
    item.insert(QStringLiteral("encoding"), analysis.encoding);
    item.insert(QStringLiteral("warnings"), analysis.warnings);
    return item;
}

void ImportService::indexImportedFiles(qint64 fileId, const QList<ImportEntry> &entries,
                                       const QVariantList &prechecked)
{
    if (m_dedup == nullptr || fileId <= 0 || entries.isEmpty())
        return;

    // 预检结果按绝对路径索引：命中同尺寸候选时已有 contentHash/size，可直接复用
    QHash<QString, QVariantMap> precheckedByPath;
    for (const QVariant &value : prechecked) {
        const QVariantMap item = value.toMap();
        const QString path = item.value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            precheckedByPath.insert(path, item);
    }

    QVariantList fingerprintEntries;
    for (const ImportEntry &entry : entries) {
        const QFileInfo info(entry.sourcePath);
        if (!MediaTypes::isImageFile(info.fileName()))
            continue;

        QVariantMap fingerprintEntry{
            {QStringLiteral("relativePath"), entry.relativePath},
            {QStringLiteral("sourcePath"), info.absoluteFilePath()},
        };
        const auto cached = precheckedByPath.constFind(info.absoluteFilePath());
        if (cached != precheckedByPath.constEnd()) {
            fingerprintEntry.insert(QStringLiteral("contentHash"),
                                    cached->value(QStringLiteral("contentHash")));
            fingerprintEntry.insert(QStringLiteral("size"), cached->value(QStringLiteral("size")));
        }
        fingerprintEntries.append(fingerprintEntry);
    }

    if (fingerprintEntries.isEmpty())
        return;

    if (m_dedup->indexPackage(fileId, fingerprintEntries) < 0) {
        qWarning().noquote() << "[import] 内容指纹写入失败，可稍后补指纹：fileId =" << fileId;
    }
}

QVariantMap ImportService::scanPreprocess(const QString &form, const QString &rootDir)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    if (!m_library->isOpen())
        return fail(QStringLiteral("没有打开的库"));
    if (!isOnline())
        return fail(QStringLiteral("实际库离线，不能导入"));

    const TypePackage *package = m_typePackages->package(form);
    if (package == nullptr)
        return fail(QStringLiteral("类型包未安装：%1").arg(form));

    AppError dbError;
    const int enabled = m_library->dbAccess()
                            .scalar(QStringLiteral("SELECT count(*) FROM type WHERE form = :form"),
                                    {{QStringLiteral("form"), form}}, &dbError)
                            .toInt();
    if (!dbError.ok())
        return fail(dbError.message);
    if (enabled != 1)
        return fail(QStringLiteral("类型未在该库启用：%1").arg(form));

    const QDir root(rootDir);
    if (!root.exists())
        return fail(QStringLiteral("预处理根目录不存在：%1").arg(QDir::toNativeSeparators(rootDir)));

    // 按类型声明的包内布局分派扫描策略
    const QString kind = package->packageLayoutKind;
    if (kind == QStringLiteral("wholeBookText"))
        return scanBookPreprocess(form, rootDir);
    if (kind == QStringLiteral("imageSequence"))
        return scanImagePreprocess(rootDir);
    if (kind == QStringLiteral("singleMedia"))
        return scanSingleMediaPreprocess(package, rootDir);
    if (kind == QStringLiteral("mediaSequence"))
        return scanMediaSequencePreprocess(package, rootDir);
    return fail(QStringLiteral("类型 %1 的包内布局暂不支持预处理扫描").arg(form));
}

QVariantMap ImportService::scanImagePreprocess(const QString &rootDir)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    const QDir root(rootDir);
    QVariantList plans;
    QStringList warnings;

    // 目录在前、散图在后，各自按名排序（开发文档 5.5）
    const QFileInfoList entries =
        root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            const QDir folder(entry.absoluteFilePath());
            QFileInfoList images;
            for (const QFileInfo &info : folder.entryInfoList(QDir::Files, QDir::Name)) {
                if (MediaTypes::isImageFile(info.fileName()))
                    images.append(info);
            }
            if (images.isEmpty()) {
                warnings.append(QStringLiteral("文件夹无图片，已跳过：%1").arg(entry.fileName()));
                continue;
            }

            QVariantMap plan = planMeta(readMetaFile(entry.absoluteFilePath()), entry.fileName(),
                                        entry.absoluteFilePath(), &warnings);
            QStringList files;
            for (const QFileInfo &image : images)
                files.append(image.absoluteFilePath());
            plan.insert(QStringLiteral("files"), files);
            plans.append(plan);
        } else if (MediaTypes::isImageFile(entry.fileName())) {
            // 根目录散图：每个文件 = 一个单图包
            plans.append(QVariantMap{
                {QStringLiteral("title"), entry.completeBaseName()},
                {QStringLiteral("creatorName"), QString()},
                {QStringLiteral("creationDate"), QString()},
                {QStringLiteral("files"), QVariantList{entry.absoluteFilePath()}},
            });
        }
        // 非图片散文件（如 meta.json 只出现在子目录里）忽略
    }

    if (plans.isEmpty())
        return fail(QStringLiteral("预处理目录下没有发现可导入的图片：%1")
                        .arg(QDir::toNativeSeparators(rootDir)));

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("plans"), plans},
        {QStringLiteral("warnings"), warnings},
    };
}

QVariantMap ImportService::scanSingleMediaPreprocess(const TypePackage *package,
                                                     const QString &rootDir)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    QVariantList plans;
    QStringList warnings;
    QHash<QString, QVariantMap> metaCache;

    // 递归扫描：每个受支持的媒体文件 = 一个包（单视频类型即"一个视频一个包"）；
    // 标题取文件主名，创作者/日期/封面取该文件所在目录的可选 meta.json
    QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (!packageAcceptsPath(*package, info.fileName()))
            continue;

        const auto cached = metaCache.constFind(info.absolutePath());
        const QVariantMap meta =
            cached != metaCache.constEnd() ? *cached : readMetaFile(info.absolutePath());
        if (cached == metaCache.constEnd())
            metaCache.insert(info.absolutePath(), meta);

        QVariantMap plan = planMeta(meta, info.completeBaseName(), info.absolutePath(), &warnings);
        // 标题逐包唯一：同一目录可能放多个视频，meta.json 的 title 不应把它们抹成同名，
        // 故单媒体包一律以文件名主名为标题；meta.json 只贡献创作者/日期/封面
        plan.insert(QStringLiteral("title"), info.completeBaseName());
        plan.insert(QStringLiteral("files"), QVariantList{info.absoluteFilePath()});
        plans.append(plan);
    }

    if (plans.isEmpty())
        return fail(QStringLiteral("预处理目录下没有发现可导入的媒体文件：%1")
                        .arg(QDir::toNativeSeparators(rootDir)));

    std::sort(plans.begin(), plans.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantList filesA = a.toMap().value(QStringLiteral("files")).toList();
        const QVariantList filesB = b.toMap().value(QStringLiteral("files")).toList();
        return filesA.value(0).toString() < filesB.value(0).toString();
    });

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("plans"), plans},
        {QStringLiteral("warnings"), warnings},
    };
}

QVariantMap ImportService::scanMediaSequencePreprocess(const TypePackage *package,
                                                       const QString &rootDir)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    const QDir root(rootDir);
    QVariantList plans;
    QStringList warnings;

    // 根目录一层文件夹 = 一个包（包内媒体按文件名排序）；根目录散文件各自成包
    const QFileInfoList entries =
        root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            const QDir folder(entry.absoluteFilePath());
            QStringList files;
            for (const QFileInfo &info : folder.entryInfoList(QDir::Files, QDir::Name)) {
                if (packageAcceptsPath(*package, info.fileName()))
                    files.append(info.absoluteFilePath());
            }
            if (files.isEmpty()) {
                warnings.append(QStringLiteral("文件夹无受支持的媒体，已跳过：%1")
                                    .arg(entry.fileName()));
                continue;
            }

            QVariantMap plan = planMeta(readMetaFile(entry.absoluteFilePath()), entry.fileName(),
                                        entry.absoluteFilePath(), &warnings);
            plan.insert(QStringLiteral("files"), files);
            plans.append(plan);
        } else if (packageAcceptsPath(*package, entry.fileName())) {
            plans.append(QVariantMap{
                {QStringLiteral("title"), entry.completeBaseName()},
                {QStringLiteral("creatorName"), QString()},
                {QStringLiteral("creationDate"), QString()},
                {QStringLiteral("files"), QVariantList{entry.absoluteFilePath()}},
            });
        }
    }

    if (plans.isEmpty())
        return fail(QStringLiteral("预处理目录下没有发现可导入的媒体文件：%1")
                        .arg(QDir::toNativeSeparators(rootDir)));

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("plans"), plans},
        {QStringLiteral("warnings"), warnings},
    };
}

QVariantMap ImportService::scanBookPreprocess(const QString &form, const QString &rootDir)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    const TypePackage *package = m_typePackages->package(form);
    if (package == nullptr)
        return fail(QStringLiteral("类型包未安装：%1").arg(form));
    const QVariantList rules = package->packageLayout.value(QStringLiteral("chapterRules")).toList();

    QVariantList plans;
    QStringList warnings;
    QHash<QString, QVariantMap> metaCache;
    qint64 previewBudget = kPreviewBudgetBytes;

    QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0)
            continue;
        // 包清单不当作源书，避免把已有包内的 config.txt 再导入一次
        if (info.fileName().compare(QStringLiteral("config.txt"), Qt::CaseInsensitive) == 0)
            continue;

        const auto cached = metaCache.constFind(info.absolutePath());
        const QVariantMap meta =
            cached != metaCache.constEnd() ? *cached : readMetaFile(info.absolutePath());
        if (cached == metaCache.constEnd())
            metaCache.insert(info.absolutePath(), meta);

        QVariantMap plan = planMeta(meta, info.completeBaseName(), info.absolutePath(), &warnings);
        plan.insert(QStringLiteral("sourcePath"), info.absoluteFilePath());
        plan.insert(QStringLiteral("chapterCount"), 0);
        plan.insert(QStringLiteral("ruleId"), QString());
        plan.insert(QStringLiteral("firstTitle"), QString());
        plan.insert(QStringLiteral("lastTitle"), QString());

        // 章节数只在预算内预览；超出预算的书在导入时再切分
        if (info.size() <= previewBudget) {
            previewBudget -= info.size();
            const BookAnalysis analysis = BookSplitter::analyze(info.absoluteFilePath(), rules);
            if (analysis.ok) {
                plan.insert(QStringLiteral("chapterCount"), analysis.chapters.size());
                plan.insert(QStringLiteral("ruleId"), analysis.ruleId);
                plan.insert(QStringLiteral("firstTitle"), analysis.chapters.first().title);
                plan.insert(QStringLiteral("lastTitle"), analysis.chapters.last().title);
                for (const QString &warning : analysis.warnings)
                    warnings.append(QStringLiteral("%1：%2").arg(info.fileName(), warning));
            } else {
                warnings.append(QStringLiteral("%1：%2").arg(info.fileName(), analysis.error));
            }
        } else {
            warnings.append(QStringLiteral("%1 文件较大，章节数将在导入时统计")
                                .arg(info.fileName()));
        }
        plans.append(plan);
    }

    if (plans.isEmpty())
        return fail(QStringLiteral("预处理目录下没有发现 .txt 小说：%1")
                        .arg(QDir::toNativeSeparators(rootDir)));

    std::sort(plans.begin(), plans.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("sourcePath")).toString()
            < b.toMap().value(QStringLiteral("sourcePath")).toString();
    });

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("plans"), plans},
        {QStringLiteral("warnings"), warnings},
    };
}

QVariantMap ImportService::analyzeBook(const QString &form, const QString &path)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    const TypePackage *package = m_typePackages->package(form);
    if (package == nullptr)
        return fail(QStringLiteral("类型包未安装：%1").arg(form));
    if (package->packageLayoutKind != QStringLiteral("wholeBookText"))
        return fail(QStringLiteral("类型 %1 不是整本文本布局，无法分析章节").arg(form));

    const QVariantList rules = package->packageLayout.value(QStringLiteral("chapterRules")).toList();
    const BookAnalysis analysis = BookSplitter::analyze(path, rules);
    if (!analysis.ok)
        return fail(analysis.error);

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("encoding"), analysis.encoding},
        {QStringLiteral("ruleId"), analysis.ruleId},
        {QStringLiteral("chapterCount"), analysis.chapters.size()},
        {QStringLiteral("firstTitle"), analysis.chapters.first().title},
        {QStringLiteral("lastTitle"), analysis.chapters.last().title},
        {QStringLiteral("warnings"), analysis.warnings},
    };
}

QString ImportService::preprocessHint(const QString &form) const
{
    const TypePackage *package = m_typePackages->package(form);
    if (package == nullptr)
        return QString();

    const QString kind = package->packageLayoutKind;
    if (kind == QStringLiteral("wholeBookText")) {
        return QStringLiteral("约定：根目录及其子目录下的每个 .txt 视为一本小说；"
                              "同目录 meta.json 可补标题/作者/创作日期/封面；"
                              "导入时按类型包声明的章节规则切分，识别不到章节则整本单章。");
    }
    if (kind == QStringLiteral("singleMedia")) {
        return QStringLiteral("约定：根目录及其子目录下的每个媒体文件视为一个数据包；"
                              "同目录 meta.json 可补标题/创作者/创作日期/封面。");
    }
    if (kind == QStringLiteral("mediaSequence")) {
        return QStringLiteral("约定：根目录每个子文件夹 = 一个数据包（包内媒体按文件名排序）；"
                              "根目录散文件各自成包；文件夹内可放 meta.json 补标题/创作者/日期/封面。");
    }
    return QStringLiteral("约定：每个子文件夹 = 一个图集包（图片按文件名排序，"
                          "可放 meta.json 补标题/画家/日期/封面）；根目录散图各自成包。");
}

QVariantMap ImportService::previewImport(const QVariantList &files)
{
    auto fail = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };

    if (!m_library->isOpen())
        return fail(QStringLiteral("没有打开的库"));

    // 预计 id：MAX(id)+1（单用户单进程，即实际将分配的值）
    AppError dbError;
    const qint64 nextId = m_library->dbAccess()
                              .scalar(QStringLiteral("SELECT IFNULL(MAX(id),0)+1 FROM file"), {},
                                      &dbError)
                              .toLongLong();
    if (!dbError.ok())
        return fail(dbError.message);

    qint64 totalSize = 0;
    for (const QVariant &file : files) {
        const QFileInfo info(file.toString());
        if (info.isFile())
            totalSize += info.size();
    }

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("id"), nextId},
        {QStringLiteral("count"), files.size()},
        {QStringLiteral("size"), totalSize},
    };
}

QVariantMap ImportService::exportPackage(qint64 id, const QString &destDir)
{
    AppError error;
    const int count = m_storage->exportPackage(id, destDir, &error);
    if (count < 0)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("count"), count}};
}

bool ImportService::isOnline() const
{
    return m_storage->isLinkOnline();
}
