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

ImportPlan ImportService::planFromMap(const QString &form, const QVariantMap &map)
{
    ImportPlan plan;
    plan.form = form;
    plan.title = map.value(QStringLiteral("title")).toString();
    plan.creatorName = map.value(QStringLiteral("creatorName")).toString();
    plan.creationDate = map.value(QStringLiteral("creationDate")).toString();
    plan.score = map.value(QStringLiteral("score"), 0).toInt();
    plan.coverSourcePath = map.value(QStringLiteral("coverSourcePath")).toString();

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
        // 扁平清单简写（imageSequence 布局）：按序号命名，顺序即包内顺序
        const QVariantList files = map.value(QStringLiteral("files")).toList();
        for (int index = 0; index < files.size(); ++index) {
            const QFileInfo info(files.at(index).toString());
            const QString suffix = info.suffix();
            ImportEntry item;
            item.sourcePath = info.absoluteFilePath();
            item.relativePath = StorageService::indexedName(index + 1)
                + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);
            plan.entries.append(item);
        }
    }

    // 封面缺省取首个图片条目；纯文本包自然为空 → 不生成封面
    if (plan.coverSourcePath.isEmpty()) {
        for (const ImportEntry &entry : plan.entries) {
            if (MediaTypes::isImageFile(entry.sourcePath)) {
                plan.coverSourcePath = entry.sourcePath;
                break;
            }
        }
    }
    return plan;
}

QVariantMap ImportService::executePlans(const QString &form, const QVariantList &plans)
{
    const TypePackage *package = m_typePackages->package(form);
    const bool bookLayout =
        package != nullptr && package->packageLayoutKind == QStringLiteral("wholeBookText");

    QVariantList results;
    QVariantList importedIds;
    int success = 0;
    int failed = 0;

    for (int index = 0; index < plans.size(); ++index) {
        const QVariantMap map = plans.at(index).toMap();
        QVariantMap item = bookLayout ? executeBookPlan(form, map, index)
                                      : executeImagePlan(form, map, index);
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

QVariantMap ImportService::executeImagePlan(const QString &form, const QVariantMap &map, int index)
{
    const ImportPlan plan = planFromMap(form, map);
    QVariantMap item{{QStringLiteral("index"), index}};

    // 写入前完全重复预检：命中即阻断，不复制任何文件
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

    ImportPlan plan = planFromMap(form, map);
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

    // 文本布局：每个 .txt 一本书，扫描规则与图片完全不同
    if (package->packageLayoutKind == QStringLiteral("wholeBookText"))
        return scanBookPreprocess(form, rootDir);
    if (package->packageLayoutKind != QStringLiteral("imageSequence"))
        return fail(QStringLiteral("类型 %1 的包内布局暂不支持预处理扫描").arg(form));

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
    if (package != nullptr && package->packageLayoutKind == QStringLiteral("wholeBookText")) {
        return QStringLiteral("约定：根目录及其子目录下的每个 .txt 视为一本小说；"
                              "同目录 meta.json 可补标题/作者/创作日期/封面；"
                              "导入时按类型包声明的章节规则切分，识别不到章节则整本单章。");
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
