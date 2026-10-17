// 受控数据库访问, 遵循纪律:
// 1. 全应用仅此创建连接(写/读连接)，按当前库切换 db 文件
// 2. 外部不持有 QSqlDatabase，只通过参数化 SQL 接口访问
// 3. 每次打开连接执行 PRAGMA foreign_keys = ON (SQLite 每连接独立)
// 创建对象是先预生成唯一连接名，对象删除时close

#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

#include "common/AppError.h"

class DbAccess
{
public:
    // 构造函数: 生成两个全局唯一的m_writeName/m_readName实例连接名
    DbAccess();
    // 析构函数: 调用close()
    ~DbAccess();

    // 设置为DbAccess对象禁止拷贝, 即禁止b(a)/b=a
    DbAccess(const DbAccess &) = delete;
    DbAccess &operator=(const DbAccess &) = delete;

    bool open(const QString &databasePath, AppError *error = nullptr);
    void close();
    bool isOpen() const { return m_write.isOpen(); }

    QString databasePath() const { return m_path; }

    // 数据库标准事务三件套, 基于m_write连接, 让一组写操作要么全部生效，要么全部当作没发生
    bool transaction(AppError *error = nullptr);    // 划定边界
    bool commit(AppError *error = nullptr);         // 确认提交
    bool rollback(AppError *error = nullptr);       // 放弃回滚

    // 两个execute重载，参数以":name"占位、params键不支持冒号，第一个作用是传给第二个
    bool execute(const QString &sql, const QVariantMap &params = {}, AppError *error = nullptr);
    bool execute(const QString &sql, const QVariantMap &params, qint64 *insertedId,
                 AppError *error = nullptr);

    // 查询三件套
    QList<QVariantMap> select(const QString &sql,       // 查询全部行，返回行字典列表，键为列名
                              const QVariantMap &params = {},
                              AppError *error = nullptr) const;
    bool selectOne(const QString &sql,                  // 查询首行
                   const QVariantMap &params,
                   QVariantMap &row,
                   AppError *error = nullptr) const;    
    QVariant scalar(const QString &sql,                 // 查询首行首列
                    const QVariantMap &params = {},
                    AppError *error = nullptr) const;

    // DDL/迁移专用：执行可能含多条以 ';' 分隔的语句（不含存储过程/触发器分号）
    bool executeRawStatements(const QString &sql, AppError *error = nullptr);

private:
    // 辅助函数: 占位符的拼写约定
    static void bind(QSqlQuery &query, const QVariantMap &params);
    // 辅助函数: 错误的形状约定
    static void fillError(AppError *error, AppError::Code code, const QSqlQuery &query);

    // 分成读写两个连接是为了是实现“一写多读”的并发操作
    QString m_path;
    QString m_writeName;
    QSqlDatabase m_write;
    QString m_readName;
    QSqlDatabase m_read;
};
