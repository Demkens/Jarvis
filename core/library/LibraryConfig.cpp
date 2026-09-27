#include "LibraryConfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

bool LibraryConfig::load(const QString &path, AppError *error)
{
    currentName.clear();
    createdDb.clear();
    m_entries.clear();

    QFile file(path);
    if (!file.exists())
        return true; // 首次启动：空配置

    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("无法读取配置文件：%1").arg(path));
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("配置文件不是合法 JSON：%1").arg(path));
        return false;
    }
    const QJsonObject root = doc.object();

    currentName = root.value(QStringLiteral("currentDb")).toString();

    const QJsonArray created = root.value(QStringLiteral("createdDb")).toArray();
    for (const QJsonValue &item : created) {
        const QString name = item.toString();
        if (!name.isEmpty() && !createdDb.contains(name))
            createdDb.append(name);
    }

    for (const QString &name : std::as_const(createdDb)) {
        const QJsonObject section = root.value(name).toObject();
        if (section.isEmpty())
            continue; // 清单里有名字但缺节：容忍，按默认值登记

        LibraryEntry entry;
        entry.name = name;
        entry.linkAddress = section.value(QStringLiteral("linkAddress")).toString();
        entry.creationDate = static_cast<qint64>(section.value(QStringLiteral("creationDate")).toDouble(0));
        entry.schemaVersion = section.value(QStringLiteral("schemaVersion")).toInt(1);

        const QJsonArray support = section.value(QStringLiteral("support")).toArray();
        for (const QJsonValue &form : support)
            entry.support.append(form.toString());

        m_entries.insert(name, entry);
    }
    return true;
}

bool LibraryConfig::save(const QString &path, AppError *error) const
{
    QJsonObject root;
    root.insert(QStringLiteral("currentDb"), currentName);

    QJsonArray created;
    for (const QString &name : createdDb)
        created.append(name);
    root.insert(QStringLiteral("createdDb"), created);

    for (const LibraryEntry &entry : entriesInOrder()) {
        QJsonObject section;
        section.insert(QStringLiteral("linkAddress"), entry.linkAddress);

        QJsonArray support;
        for (const QString &form : entry.support)
            support.append(form);
        section.insert(QStringLiteral("support"), support);

        section.insert(QStringLiteral("creationDate"), static_cast<double>(entry.creationDate));
        section.insert(QStringLiteral("schemaVersion"), entry.schemaVersion);
        root.insert(entry.name, section);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("无法写入配置文件：%1").arg(path));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("配置文件提交失败：%1").arg(path));
        return false;
    }
    return true;
}

bool LibraryConfig::contains(const QString &name) const
{
    return m_entries.contains(name);
}

LibraryEntry LibraryConfig::entry(const QString &name) const
{
    return m_entries.value(name, LibraryEntry{name, {}, {}, 0, 1});
}

void LibraryConfig::upsert(const LibraryEntry &entry)
{
    if (!createdDb.contains(entry.name))
        createdDb.append(entry.name);
    m_entries.insert(entry.name, entry);
}

void LibraryConfig::remove(const QString &name)
{
    createdDb.removeAll(name);
    m_entries.remove(name);
    if (currentName == name)
        currentName = createdDb.isEmpty() ? QString() : createdDb.first();
}

QList<LibraryEntry> LibraryConfig::entriesInOrder() const
{
    QList<LibraryEntry> result;
    result.reserve(createdDb.size());
    for (const QString &name : createdDb) {
        const auto it = m_entries.constFind(name);
        if (it != m_entries.constEnd())
            result.append(it.value());
    }
    return result;
}
