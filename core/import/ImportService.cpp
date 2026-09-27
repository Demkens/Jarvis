#include "ImportService.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "library/LibraryService.h"
#include "storage/StorageService.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

namespace {

QStringList kImageSuffixes = {
    QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
    QStringLiteral("gif"),  QStringLiteral("bmp"),  QStringLiteral("webp"),
};

} // namespace

ImportService::ImportService(LibraryService *library, StorageService *storage,
                             TypePackageManager *typePackages, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_storage(storage)
    , m_typePackages(typePackages)
{
}

bool ImportService::isImageFile(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return kImageSuffixes.contains(suffix);
}

ImportPlan ImportService::planFromMap(const QString &form, const QVariantMap &map)
{
    ImportPlan plan;
    plan.form = form;
    plan.title = map.value(QStringLiteral("title")).toString();
    plan.creatorName = map.value(QStringLiteral("creatorName")).toString();
    plan.creationDate = map.value(QStringLiteral("creationDate")).toString();
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
        AppError error;
        const qint64 id = m_storage->importPackage(plan, &error);

        QVariantMap item{{QStringLiteral("index"), index}};
        if (id > 0) {
            item.insert(QStringLiteral("ok"), true);
            item.insert(QStringLiteral("id"), id);
            importedIds.append(id);
            ++success;
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
                if (isImageFile(info.fileName()))
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
        } else if (isImageFile(entry.fileName())) {
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
