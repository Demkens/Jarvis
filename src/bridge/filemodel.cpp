#include "filemodel.h"
#include <QSqlQuery>
#include <QFileInfo>
#include <QDebug>
#include <QSqlError>

FileModel::FileModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // 创建独立的数据库连接，避免连接名冲突
    dbLink = QSqlDatabase::addDatabase("QSQLITE", "FileConnection");
}

FileModel::~FileModel()
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    QSqlDatabase::removeDatabase("FileConnection");
}

int FileModel::rowCount(const QModelIndex &parent) const
{
    // 对于列表模型，父索引有效时返回0
    if (parent.isValid()) {
        return 0;
    }
    return m_files.size();
}

QVariant FileModel::data(const QModelIndex &index, int role) const
{
    // 检查索引有效性
    if (!index.isValid() || index.row() >= m_files.size()) {
        return QVariant();
    }

    const FileItem &item = m_files.at(index.row());

    // 根据角色返回对应数据
    switch (role) {
        case IdRole:         return item.id;
        case NameRole:       return item.name;
        case PathRole:       return item.path;
        case TypeRole:       return item.type;
        case ThumbnailRole:  return item.thumbnail;
        case CreateTimeRole: return item.createTime;
        default:             return QVariant();
    }
}

QHash<int, QByteArray> FileModel::roleNames() const
{
    // 定义角色名称映射，供QML识别
    QHash<int, QByteArray> roles;
    roles[IdRole]         = "id";
    roles[NameRole]       = "name";
    roles[PathRole]       = "path";
    roles[TypeRole]       = "type";
    roles[ThumbnailRole]  = "thumbnail";
    roles[CreateTimeRole] = "createTime";
    return roles;
}

void FileModel::refresh(const QString &dbPath)
{
    // 关闭现有连接并重置路径
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    dbLink.setDatabaseName(dbPath);

    if (!dbLink.open()) {
        qWarning() << "FileModel: 数据库连接失败" << dbLink.lastError().text();
        return;
    }

    // 开始模型重置，通知视图数据即将更新
    beginResetModel();
    m_files.clear();

    QSqlQuery query(dbLink);
    // 按ID倒序查询，获取最新文件优先显示
    if (query.exec("SELECT id, name, path, type, thumbnail, create_time FROM files ORDER BY id DESC")) {
        while (query.next()) {
            FileItem item;
            item.id         = query.value(0).toInt();
            item.name       = query.value(1).toString();
            item.path       = query.value(2).toString();
            item.type       = query.value(3).toString();
            item.thumbnail  = query.value(4).toString();
            item.createTime = query.value(5).toDateTime();
            m_files.append(item);
        }
    } else {
        qWarning() << "FileModel: 查询失败" << query.lastError().text();
    }

    // 结束模型重置
    endResetModel();
    emit fileCountChanged();
}

bool FileModel::addFile(const QString &filePath, const QString &fileType)
{
    // 检查文件是否存在
    QFileInfo info(filePath);
    if (!info.exists()) {
        qWarning() << "FileModel: 文件不存在" << filePath;
        return false;
    }

    QSqlQuery query(dbLink);
    query.prepare("INSERT INTO files (name, path, type, create_time) VALUES (?, ?, ?, ?)");
    query.addBindValue(info.fileName());   // 使用文件名
    query.addBindValue(filePath);          // 文件路径
    query.addBindValue(fileType);          // 文件类型
    query.addBindValue(QDateTime::currentDateTime()); // 创建时间

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "FileModel: 添加文件失败" << query.lastError().text();
    return false;
}

bool FileModel::removeFile(int fileId)
{
    QSqlQuery query(dbLink);
    query.prepare("DELETE FROM files WHERE id = ?");
    query.addBindValue(fileId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "FileModel: 删除文件失败" << query.lastError().text();
    return false;
}

bool FileModel::updateFile(int fileId, const QString &name, const QString &path)
{
    QSqlQuery query(dbLink);
    query.prepare("UPDATE files SET name = ?, path = ? WHERE id = ?");
    query.addBindValue(name);
    query.addBindValue(path);
    query.addBindValue(fileId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "FileModel: 更新文件失败" << query.lastError().text();
    return false;
}

QVariantMap FileModel::getFile(int fileId) const
{
    QVariantMap map;
    for (const auto &item : m_files) {
        if (item.id == fileId) {
            map["id"]          = item.id;
            map["name"]        = item.name;
            map["path"]        = item.path;
            map["type"]        = item.type;
            map["thumbnail"]   = item.thumbnail;
            map["createTime"]  = item.createTime;
            break;
        }
    }
    return map;
}
