#include "datacountermodel.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

DataCounterModel::DataCounterModel(QObject *parent)
    : QObject(parent)
    , m_totalFiles(0)
    , m_totalLabels(0)
    , m_totalSize(0)
{
    dbLink = QSqlDatabase::addDatabase("QSQLITE", "CounterConnection");
}

DataCounterModel::~DataCounterModel()
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    QSqlDatabase::removeDatabase("CounterConnection");
}

void DataCounterModel::refresh(const QString &dbPath)
{
    if (dbLink.isOpen()) {
        dbLink.close();
    }
    dbLink.setDatabaseName(dbPath);

    if (!dbLink.open()) {
        qWarning() << "DataCounterModel: 数据库连接失败" << dbLink.lastError().text();
        return;
    }

    QSqlQuery query(dbLink);

    // 统计文件总数
    if (query.exec("SELECT COUNT(*) FROM files")) {
        if (query.next()) {
            m_totalFiles = query.value(0).toInt();
        }
    } else {
        qWarning() << "DataCounterModel: 统计文件数失败" << query.lastError().text();
    }

    // 统计标签总数
    if (query.exec("SELECT COUNT(*) FROM labels")) {
        if (query.next()) {
            m_totalLabels = query.value(0).toInt();
        }
    } else {
        qWarning() << "DataCounterModel: 统计标签数失败" << query.lastError().text();
    }

    // 统计文件总大小（按路径非空统计）
    if (query.exec("SELECT SUM(CASE WHEN path != '' THEN 1 ELSE 0 END) FROM files")) {
        if (query.next()) {
            m_totalSize = query.value(0).toLongLong();
        }
    } else {
        qWarning() << "DataCounterModel: 统计大小失败" << query.lastError().text();
    }

    emit statsChanged();
}

QVariantMap DataCounterModel::getFileTypeStats()
{
    QVariantMap map;
    QSqlQuery query(dbLink);

    // 按文件类型分组统计
    if (query.exec("SELECT type, COUNT(*) FROM files GROUP BY type")) {
        while (query.next()) {
            map[query.value(0).toString()] = query.value(1).toInt();
        }
    } else {
        qWarning() << "DataCounterModel: 文件类型统计失败" << query.lastError().text();
    }

    return map;
}

QVariantMap DataCounterModel::getTimeLineStats()
{
    QVariantMap map;
    QSqlQuery query(dbLink);

    // 按日期分组统计（最近30天）
    if (query.exec("SELECT DATE(create_time) as date, COUNT(*) FROM files GROUP BY date ORDER BY date DESC LIMIT 30")) {
        while (query.next()) {
            map[query.value(0).toString()] = query.value(1).toInt();
        }
    } else {
        qWarning() << "DataCounterModel: 时间线统计失败" << query.lastError().text();
    }

    return map;
}

QVariantMap DataCounterModel::getLabelDistribution()
{
    QVariantMap map;
    QSqlQuery query(dbLink);

    // 统计每个标签关联的文件数量
    if (query.exec("SELECT l.name, COUNT(fl.file_id) FROM labels l LEFT JOIN file_labels fl ON l.id = fl.label_id GROUP BY l.id")) {
        while (query.next()) {
            map[query.value(0).toString()] = query.value(1).toInt();
        }
    } else {
        qWarning() << "DataCounterModel: 标签分布统计失败" << query.lastError().text();
    }

    return map;
}
