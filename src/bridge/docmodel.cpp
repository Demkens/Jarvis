#include "docmodel.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

DocModel::DocModel(QObject *parent)
    : QObject(parent)
{
    dbLink = QSqlDatabase::addDatabase("QSQLITE", "DocConnection");
}

DocModel::~DocModel()
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    QSqlDatabase::removeDatabase("DocConnection");
}

void DocModel::refresh(const QString &dbPath)
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    dbLink.setDatabaseName(dbPath);

    if (!dbLink.open()) {
        qWarning() << "DocModel: 数据库连接失败" << dbLink.lastError().text();
        return;
    }

    m_docs.clear();

    QSqlQuery query(dbLink);
    // 按更新时间倒序查询
    if (query.exec("SELECT id, title, content, linked_file_id, create_time, update_time FROM docs ORDER BY id DESC")) {
        while (query.next()) {
            DocItem item;
            item.id           = query.value(0).toInt();
            item.title        = query.value(1).toString();
            item.content      = query.value(2).toString();
            item.linkedFileId = query.value(3).toInt();
            item.createTime   = query.value(4).toDateTime();
            item.updateTime   = query.value(5).toDateTime();
            m_docs.append(item);
        }
    } else {
        qWarning() << "DocModel: 查询失败" << query.lastError().text();
    }
}

bool DocModel::createDoc(const QString &title, const QString &content, int linkedFileId)
{
    QSqlQuery query(dbLink);
    query.prepare("INSERT INTO docs (title, content, linked_file_id, create_time, update_time) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(title);
    query.addBindValue(content);
    query.addBindValue(linkedFileId);
    query.addBindValue(QDateTime::currentDateTime());
    query.addBindValue(QDateTime::currentDateTime());

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "DocModel: 创建文档失败" << query.lastError().text();
    return false;
}

bool DocModel::updateDoc(int docId, const QString &title, const QString &content)
{
    QSqlQuery query(dbLink);
    query.prepare("UPDATE docs SET title = ?, content = ?, update_time = ? WHERE id = ?");
    query.addBindValue(title);
    query.addBindValue(content);
    query.addBindValue(QDateTime::currentDateTime());
    query.addBindValue(docId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "DocModel: 更新文档失败" << query.lastError().text();
    return false;
}

bool DocModel::deleteDoc(int docId)
{
    QSqlQuery query(dbLink);
    query.prepare("DELETE FROM docs WHERE id = ?");
    query.addBindValue(docId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "DocModel: 删除文档失败" << query.lastError().text();
    return false;
}

QVariantMap DocModel::getDoc(int docId)
{
    QVariantMap map;
    for (const auto &doc : m_docs) {
        if (doc.id == docId) {
            map["id"]           = doc.id;
            map["title"]        = doc.title;
            map["content"]      = doc.content;
            map["linkedFileId"] = doc.linkedFileId;
            map["createTime"]   = doc.createTime;
            map["updateTime"]   = doc.updateTime;
            break;
        }
    }
    return map;
}

QVariantList DocModel::getAllDocs()
{
    QVariantList list;
    for (const auto &doc : m_docs) {
        QVariantMap map;
        map["id"]           = doc.id;
        map["title"]        = doc.title;
        map["linkedFileId"] = doc.linkedFileId;
        map["createTime"]    = doc.createTime;
        list.append(map);
    }
    return list;
}

QVariantList DocModel::getFileDocs(int fileId)
{
    QVariantList list;
    for (const auto &doc : m_docs) {
        if (doc.linkedFileId == fileId) {
            QVariantMap map;
            map["id"]      = doc.id;
            map["title"]   = doc.title;
            map["content"] = doc.content;
            list.append(map);
        }
    }
    return list;
}

bool DocModel::linkDocToFile(int docId, int fileId)
{
    QSqlQuery query(dbLink);
    query.prepare("UPDATE docs SET linked_file_id = ? WHERE id = ?");
    query.addBindValue(fileId);
    query.addBindValue(docId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "DocModel: 关联文档失败" << query.lastError().text();
    return false;
}

bool DocModel::unlinkDocFromFile(int docId, int fileId)
{
    QSqlQuery query(dbLink);
    query.prepare("UPDATE docs SET linked_file_id = -1 WHERE id = ? AND linked_file_id = ?");
    query.addBindValue(docId);
    query.addBindValue(fileId);

    if (query.exec()) {
        refresh(dbLink.databaseName());
        return true;
    }

    qWarning() << "DocModel: 取消关联失败" << query.lastError().text();
    return false;
}
