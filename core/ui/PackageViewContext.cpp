#include "PackageViewContext.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "storage/StorageService.h"
#include "text/BookSplitter.h"

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

    // 包内文件清单（物理路径由核心拼接）
    const QString libraryName = m_library->currentName();
    const QString linkAddress = m_library->config().entry(libraryName).linkAddress;
    const int bucket = detail.value(QStringLiteral("storage_path")).toInt();
    m_packageDir = StorageService::packageDir(linkAddress, bucket, m_packageId);

    // 递归列出内容文件（小说包有章节文件，图集包是扁平序列）；
    // 包清单 config.txt 属元数据、不进内容清单，需要时由类型包自行 readText 读取
    QStringList relativePaths;
    QDirIterator it(m_packageDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative =
            QDir(m_packageDir).relativeFilePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (relative == QStringLiteral("config.txt"))
            continue;
        relativePaths.append(relative);
    }
    relativePaths.sort(Qt::CaseInsensitive);

    int index = 0;
    for (const QString &relative : relativePaths) {
        ++index;
        const QString absolute = m_packageDir + QLatin1Char('/') + relative;
        const QFileInfo info(absolute);
        m_files.append(QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("relativePath"), relative},
            {QStringLiteral("size"), info.size()},
            {QStringLiteral("url"), QUrl::fromLocalFile(absolute)},
        });
    }

    m_position = m_packages->browseState(m_packageId, &error);
    loadManifest();

    emit dataChanged();
    emit positionChanged();
}

void PackageViewContext::loadManifest()
{
    m_chapters.clear();
    m_volumes.clear();

    QFile manifest(m_packageDir + QStringLiteral("/config.txt"));
    if (!manifest.open(QIODevice::ReadOnly))
        return;  // 非文本包没有清单，属正常情形

    const QJsonDocument doc = QJsonDocument::fromJson(manifest.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();

    QHash<int, QString> volumeNames;
    for (const QJsonValue &value : root.value(QStringLiteral("volumes")).toArray()) {
        const QJsonObject volume = value.toObject();
        const int index = volume.value(QStringLiteral("index")).toInt();
        const QString name = volume.value(QStringLiteral("name")).toString();
        volumeNames.insert(index, name);
        m_volumes.append(QVariantMap{{QStringLiteral("index"), index},
                                     {QStringLiteral("name"), name}});
    }

    for (const QJsonValue &value : root.value(QStringLiteral("chapters")).toArray()) {
        const QJsonObject chapter = value.toObject();
        const int volume = chapter.value(QStringLiteral("volume")).toInt();
        m_chapters.append(QVariantMap{
            {QStringLiteral("file"), chapter.value(QStringLiteral("file")).toString()},
            {QStringLiteral("title"), chapter.value(QStringLiteral("title")).toString()},
            {QStringLiteral("volume"), volume},
            {QStringLiteral("volumeName"), volumeNames.value(volume)},
        });
    }
}

QString PackageViewContext::readText(const QString &relativePath) const
{
    // 只允许包内相对路径，拒绝绝对路径与 ".."（本接口对类型包开放）
    const QString relative = relativePath.trimmed();
    if (relative.isEmpty() || relative.startsWith(QLatin1Char('/'))
        || relative.contains(QLatin1Char(':')) || relative.contains(QStringLiteral(".."))) {
        qWarning().noquote() << "[viewer] 拒绝读取非法相对路径：" << relativePath;
        return QString();
    }

    QFile file(m_packageDir + QLatin1Char('/') + relative);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[viewer] 文本文件不存在或不可读：" << relative;
        return QString();
    }
    QString encoding;
    const QString text = BookSplitter::decodeText(file.readAll(), &encoding);
    if (text.isEmpty() && file.size() > 0)
        qWarning().noquote() << "[viewer] 文本解码失败：" << relative;
    return text;
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
