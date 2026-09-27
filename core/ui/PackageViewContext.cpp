#include "PackageViewContext.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "storage/StorageService.h"
#include "types/TypeEngine.h"

PackageViewContext::PackageViewContext(qint64 packageId, AppPaths *paths, LibraryService *library,
                                       PackageService *packages, TypeEngine *engine,
                                       QObject *parent)
    : QObject(parent)
    , m_packageId(packageId)
    , m_paths(paths)
    , m_library(library)
    , m_packages(packages)
    , m_engine(engine)
{
    load();
}

void PackageViewContext::load()
{
    AppError error;
    const QVariantMap detail = m_packages->packageDetail(m_packageId, &error);
    if (!error.ok() || detail.isEmpty())
        return;

    m_title = detail.value(QStringLiteral("title")).toString();
    m_typeForm = detail.value(QStringLiteral("type_form")).toString();

    // 分表字段（剔除 file 总表列，只留类型专属字段 + 解析后的创作者名）
    static const QSet<QString> kFileColumns = {
        QStringLiteral("id"),       QStringLiteral("title"),     QStringLiteral("type_id"),
        QStringLiteral("score"),    QStringLiteral("size"),      QStringLiteral("storage_path"),
        QStringLiteral("created_time"), QStringLiteral("updated_time"),
        QStringLiteral("type_form")};
    for (auto it = detail.constBegin(); it != detail.constEnd(); ++it) {
        if (!kFileColumns.contains(it.key()))
            m_fields.insert(it.key(), it.value());
    }

    // 包内文件清单（物理路径由核心拼接，顺序按文件名，即 001→002…）
    const QString libraryName = m_library->currentName();
    const QString linkAddress = m_library->config().entry(libraryName).linkAddress;
    const int bucket = detail.value(QStringLiteral("storage_path")).toInt();
    const QString packageDir = StorageService::packageDir(linkAddress, bucket, m_packageId);

    int index = 0;
    const QDir dir(packageDir);
    for (const QFileInfo &info :
         dir.entryInfoList(QDir::Files, QDir::Name)) {
        ++index;
        m_files.append(QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("relativePath"), info.fileName()},
            {QStringLiteral("size"), info.size()},
            {QStringLiteral("url"), QUrl::fromLocalFile(info.absoluteFilePath())},
        });
    }

    m_position = m_packages->browseState(m_packageId, &error);

    emit dataChanged();
    emit fieldsChanged();
    emit positionChanged();
}

void PackageViewContext::setPositionJson(const QString &json)
{
    if (m_position == json)
        return;
    m_position = json;
    emit positionChanged();
}

bool PackageViewContext::savePosition()
{
    AppError error;
    if (!m_packages->writeBrowseState(m_packageId, m_position, &error)) {
        qWarning().noquote() << "[viewer] 浏览位置写回失败:" << error.message;
        return false;
    }
    return true;
}

QVariantMap PackageViewContext::writeField(const QString &name, const QVariant &value)
{
    QVariantMap values;
    values.insert(name, value);
    const QVariantMap result = m_engine->writeFields(m_packageId, values);
    if (result.value(QStringLiteral("ok")).toBool())
        reloadMeta();
    return result;
}

void PackageViewContext::reloadMeta()
{
    AppError error;
    const QVariantMap detail = m_packages->packageDetail(m_packageId, &error);
    if (!error.ok())
        return;

    m_title = detail.value(QStringLiteral("title")).toString();
    static const QSet<QString> kFileColumns = {
        QStringLiteral("id"),       QStringLiteral("title"),     QStringLiteral("type_id"),
        QStringLiteral("score"),    QStringLiteral("size"),      QStringLiteral("storage_path"),
        QStringLiteral("created_time"), QStringLiteral("updated_time"),
        QStringLiteral("type_form")};
    m_fields.clear();
    for (auto it = detail.constBegin(); it != detail.constEnd(); ++it) {
        if (!kFileColumns.contains(it.key()))
            m_fields.insert(it.key(), it.value());
    }
    emit dataChanged();
    emit fieldsChanged();
}
