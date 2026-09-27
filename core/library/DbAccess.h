#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

#include "common/AppError.h"

// 受控数据库访问
// 纪律：
// - 全应用仅此创建连接（写连接 + 读连接），按当前库切换 db 文件；
// - 外部（含未来类型包）不持有 QSqlDatabase，只通过参数化 SQL 接口访问；
// - 每次打开连接执行 PRAGMA foreign_keys = ON（SQLite 每连接独立）。
class DbAccess
{
public:
    DbAccess();
    ~DbAccess();

    DbAccess(const DbAccess &) = delete;
    DbAccess &operator=(const DbAccess &) = delete;

    bool open(const QString &databasePath, AppError *error = nullptr);
    void close();
    bool isOpen() const { return m_write.isOpen(); }
    QString databasePath() const { return m_path; }

    // 事务（开发文档 3.7：多语句写操作必须在事务内）
    bool transaction(AppError *error = nullptr);
    bool commit(AppError *error = nullptr);
    bool rollback(AppError *error = nullptr);

    // 单条写语句，参数以 ":name" 占位、params 键不支持冒号
    bool execute(const QString &sql, const QVariantMap &params = {}, AppError *error = nullptr);

    // 同上；insertedId 非空时接收自增主键（lastInsertId），供"插入占位行取 id"使用
    bool execute(const QString &sql, const QVariantMap &params, qint64 *insertedId,
                 AppError *error = nullptr);

    // 查询：返回行字典列表（键为列名）
    QList<QVariantMap> select(const QString &sql,
                              const QVariantMap &params = {},
                              AppError *error = nullptr) const;

    // 查询首行；无结果时 ok 为 true、row 为空 map（用是否含列名区分）
    bool selectOne(const QString &sql,
                   const QVariantMap &params,
                   QVariantMap &row,
                   AppError *error = nullptr) const;

    // 查询首行首列（如 PRAGMA、count）
    QVariant scalar(const QString &sql,
                    const QVariantMap &params = {},
                    AppError *error = nullptr) const;

    // DDL/迁移专用：执行可能含多条以 ';' 分隔的语句（不含存储过程/触发器分号）
    bool executeRawStatements(const QString &sql, AppError *error = nullptr);

private:
    static void bind(QSqlQuery &query, const QVariantMap &params);
    static void fillError(AppError *error, AppError::Code code, const QSqlQuery &query);

    QSqlDatabase m_write;
    QSqlDatabase m_read;
    QString m_path;
    // 每实例唯一的连接名：多个 DbAccess 并存时（如新旧服务交替）不互相摘除
    QString m_writeName;
    QString m_readName;
};
