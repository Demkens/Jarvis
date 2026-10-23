#include "ImportService.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "common/MediaTypes.h"
#include "dedup/DedupService.h"
#include "library/LibraryService.h"
#include "storage/StorageService.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

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
    const QVariantList files = map.value(QStringLiteral("files")).toList();
    for (const QVariant &file : files)
        plan.files.append(file.toString());
    return plan;
}

QVariantMap ImportService::executePlans(const QString &form, const QVariantList &plans)
{
    QVariantList results;
    QVariantList importedIds;
    int success = 0;
    int failed = 0;

    for (int index = 0; index < plans.size(); ++index) {
        const ImportPlan plan = planFromMap(form, plans.at(index).toMap());
        QVariantMap item{{QStringLiteral("index"), index}};

        // 写入前完全重复预检：命中即阻断，不复制任何文件
        QVariantMap precheck{{QStringLiteral("ok"), true},
                             {QStringLiteral("hasDuplicates"), false},
                             {QStringLiteral("files"), QVariantList{}}};
        if (m_dedup != nullptr) {
            QVariantList paths;
            for (const QString &file : plan.files)
                paths.append(file);
            precheck = m_dedup->precheck(paths);
            if (!precheck.value(QStringLiteral("ok")).toBool()) {
                item.insert(QStringLiteral("ok"), false);
                item.insert(QStringLiteral("message"),
                            precheck.value(QStringLiteral("message")).toString());
                ++failed;
                results.append(item);
                continue;
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
                ++failed;
                results.append(item);
                continue;
            }
        }

        AppError error;
        const qint64 id = m_storage->importPackage(plan, &error);

        if (id > 0) {
            item.insert(QStringLiteral("ok"), true);
            item.insert(QStringLiteral("id"), id);
            importedIds.append(id);
            ++success;
            // 指纹写入失败不影响导入结果，后续可由「补指纹」入口修复；
            // 预检已算出的哈希会随 prechecked 传入，避免二次读盘
            indexImportedFiles(id, plan.files,
                               precheck.value(QStringLiteral("files")).toList());
        } else {
            item.insert(QStringLiteral("ok"), false);
            item.insert(QStringLiteral("message"), error.message);
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

void ImportService::indexImportedFiles(qint64 fileId, const QStringList &files,
                                       const QVariantList &prechecked)
{
    if (m_dedup == nullptr || fileId <= 0 || files.isEmpty())
        return;

    // 预检结果按绝对路径索引：命中同尺寸候选时已有 contentHash/size，可直接复用
    QHash<QString, QVariantMap> precheckedByPath;
    for (const QVariant &value : prechecked) {
        const QVariantMap item = value.toMap();
        const QString path = item.value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            precheckedByPath.insert(path, item);
    }

    QVariantList entries;
    for (int index = 0; index < files.size(); ++index) {
        const QFileInfo info(files.at(index));
        if (!MediaTypes::isImageFile(info.fileName()))
            continue;

        // 包内文件按导入顺序重命名为 001.ext，指纹表使用同一相对路径作为主键
        const QString suffix = info.suffix();
        const QString relativePath = StorageService::indexedName(index + 1)
            + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);

        QVariantMap entry{
            {QStringLiteral("relativePath"), relativePath},
            {QStringLiteral("sourcePath"), info.absoluteFilePath()},
        };
        const auto cached = precheckedByPath.constFind(info.absoluteFilePath());
        if (cached != precheckedByPath.constEnd()) {
            entry.insert(QStringLiteral("contentHash"),
                         cached->value(QStringLiteral("contentHash")));
            entry.insert(QStringLiteral("size"), cached->value(QStringLiteral("size")));
        }
        entries.append(entry);
    }

    if (entries.isEmpty())
        return;

    if (m_dedup->indexPackage(fileId, entries) < 0) {
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
    if (package->packageLayoutKind != QStringLiteral("imageSequence"))
        return fail(QStringLiteral("类型 %1 的预处理扫描一期不支持（仅支持图片序列）").arg(form));

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

    // 读取 meta.json（缺省/损坏均不致命，仅忽略元数据）
    auto readMeta = [](const QString &dirPath, QVariantMap &meta) {
        QFile file(dirPath + QStringLiteral("/meta.json"));
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject())
            meta = doc.object().toVariantMap();
    };

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

            QVariantMap meta;
            readMeta(entry.absoluteFilePath(), meta);

            QStringList files;
            for (const QFileInfo &image : images)
                files.append(image.absoluteFilePath());

            QVariantMap plan{
                {QStringLiteral("title"),
                 meta.value(QStringLiteral("title")).toString().isEmpty()
                     ? entry.fileName()
                     : meta.value(QStringLiteral("title")).toString()},
                {QStringLiteral("creatorName"), meta.value(QStringLiteral("creator")).toString()},
                {QStringLiteral("creationDate"),
                 meta.value(QStringLiteral("creation_date")).toString()},
                {QStringLiteral("files"), files},
            };

            // cover：meta 指定的文件名（须在图片清单内），缺省取序号第一张
            const QString coverName = meta.value(QStringLiteral("cover")).toString();
            if (!coverName.isEmpty()) {
                const QString coverPath = entry.absoluteFilePath() + QLatin1Char('/') + coverName;
                if (QFileInfo::exists(coverPath))
                    plan.insert(QStringLiteral("coverSourcePath"), coverPath);
                else
                    warnings.append(QStringLiteral("meta.json 指定的封面不存在，已用首图：%1/%2")
                                        .arg(entry.fileName(), coverName));
            }
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
