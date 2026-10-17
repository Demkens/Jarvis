#include "PackageViewContext.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "storage/StorageService.h"

PackageViewContext::PackageViewContext(qint64 packageId, AppPaths *paths, LibraryService *library,
                                       PackageService *packages, QObject *parent)
    : QObject(parent)
    , m_packageId(packageId)
    , m_paths(paths)
    , m_library(library)
    , m_packages(packages)
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
