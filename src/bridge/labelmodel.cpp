#include "labelmodel.h"
#include <QSqlQuery>
#include <QDebug>
#include <QSqlError>

LabelModel::LabelModel(QObject *parent)
    : QAbstractListModel(parent)
{
    dbLink = QSqlDatabase::addDatabase("QSQLITE", "LabelConnection");
}

LabelModel::~LabelModel()
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    QSqlDatabase::removeDatabase("LabelConnection");
}

int LabelModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_labels.size();
}

QVariant LabelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_labels.size()) {
        return QVariant();
    }

    const LabelItem &item = m_labels.at(index.row());

    switch (role) {
        case IdRole:    return item.id;
        case NameRole:  return item.name;
        case ColorRole: return item.color;
        default:         return QVariant();
    }
}

QHash<int, QByteArray> LabelModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole]    = "id";
    roles[NameRole]  = "name";
    roles[ColorRole] = "color";
    return roles;
}

void LabelModel::refresh(const QString &dbPath)
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    dbLink.setDatabaseName(dbPath);

    if (!dbLink.open()) {
        qWarning() << "LabelModel: 数据库连接失败" << dbLink.lastError().text();
        return;
    }

    beginResetModel();
    m_labels.clear();
    m_relations.clear();

    QSqlQuery query(dbLink);

    // 查询标签列表
    if (query.exec("SELECT id, name, color FROM labels ORDER BY id")) {
        while (query.next()) {
            LabelItem item;
            item.id    = query.value(0).toInt();
            item.name  = query.value(1).toString();
            item.color = query.value(2).toString();
            m_labels.append(item);
        }
    } else {
        qWarning() << "LabelModel: 查询标签失败" << query.lastError().text();
    }

    // 查询标签关系
    if (query.exec("SELECT label_id1, label_id2, relation_type FROM label_relations")) {
        while (query.next()) {
            RelationItem item;
            item.labelId1      = query.value(0).toInt();
            item.labelId2      = query.value(1).toInt();
            item.relationType  = query.value(2).toString();
            m_relations.append(item);
        }
    }

    endResetModel();
    emit labelCountChanged();
}

bool LabelModel::addLabel(const QString &name, const QString &color)
{
    QSqlQuery query(dbLink);
    query.prepare("INSERT INTO labels (name, color) VALUES (?, ?)");
    query.addBindValue(name);
    query.addBindValue(color);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "LabelModel: 添加标签失败" << query.lastError().text();
    return false;
}

bool LabelModel::removeLabel(int labelId)
{
    QSqlQuery query(dbLink);
    query.prepare("DELETE FROM labels WHERE id = ?");
    query.addBindValue(labelId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "LabelModel: 删除标签失败" << query.lastError().text();
    return false;
}

bool LabelModel::updateLabel(int labelId, const QString &name, const QString &color)
{
    QSqlQuery query(dbLink);
    query.prepare("UPDATE labels SET name = ?, color = ? WHERE id = ?");
    query.addBindValue(name);
    query.addBindValue(color);
    query.addBindValue(labelId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "LabelModel: 更新标签失败" << query.lastError().text();
    return false;
}

bool LabelModel::addRelation(int labelId1, int labelId2, const QString &relationType)
{
    QSqlQuery query(dbLink);
    query.prepare("INSERT INTO label_relations (label_id1, label_id2, relation_type) VALUES (?, ?, ?)");
    query.addBindValue(labelId1);
    query.addBindValue(labelId2);
    query.addBindValue(relationType);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "LabelModel: 添加关系失败" << query.lastError().text();
    return false;
}

bool LabelModel::removeRelation(int labelId1, int labelId2)
{
    QSqlQuery query(dbLink);
    query.prepare("DELETE FROM label_relations WHERE label_id1 = ? AND label_id2 = ?");
    query.addBindValue(labelId1);
    query.addBindValue(labelId2);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "LabelModel: 删除关系失败" << query.lastError().text();
    return false;
}

QVariantList LabelModel::getLabelRelations(int labelId)
{
    QVariantList list;
    for (const auto &rel : m_relations) {
        if (rel.labelId1 == labelId || rel.labelId2 == labelId) {
            QVariantMap map;
            map["labelId1"]      = rel.labelId1;
            map["labelId2"]      = rel.labelId2;
            map["relationType"]  = rel.relationType;
            list.append(map);
        }
    }
    return list;
}

QVariantList LabelModel::getFileLabels(int fileId)
{
    QVariantList list;
    QSqlQuery query(dbLink);
    query.prepare("SELECT label_id FROM file_labels WHERE file_id = ?");
    query.addBindValue(fileId);

    if (query.exec()) {
        while (query.next()) {
            list.append(query.value(0).toInt());
        }
    }
    return list;
}

bool LabelModel::setFileLabels(int fileId, const QVariantList &labelIds)
{
    QSqlQuery query(dbLink);

    // 先删除文件的所有标签关联
    query.prepare("DELETE FROM file_labels WHERE file_id = ?");
    query.addBindValue(fileId);
    query.exec();

    // 重新插入标签关联
    for (const auto &id : labelIds) {
        query.prepare("INSERT INTO file_labels (file_id, label_id) VALUES (?, ?)");
        query.addBindValue(fileId);
        query.addBindValue(id.toInt());
        query.exec();
    }

    return true;
}
