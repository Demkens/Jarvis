#include "DbAccess.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <atomic>

namespace {

QString makeConnectionName(const char *role)
{
    static std::atomic<int> counter{0};
    return QStringLiteral("jarvis.%1.%2")
        .arg(QString::fromLatin1(role))
        .arg(++counter);
}

} // namespace

DbAccess::DbAccess()
    : m_writeName(makeConnectionName("write"))
    , m_readName(makeConnectionName("read"))
{
}

DbAccess::~DbAccess()
{
    close();
}

bool DbAccess::open(const QString &databasePath, AppError *error)
{
    close();

    if (!QFileInfo::exists(databasePath)) {
        if (error != nullptr) {
            *error = AppError::fail(AppError::Db,
                                    QStringLiteral("数据库文件不存在：%1").arg(databasePath));
        }
        return false;
    }

    m_write = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_writeName);
    m_read = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_readName);
    m_write.setDatabaseName(databasePath);
    m_read.setDatabaseName(databasePath);

    if (!m_write.open() || !m_read.open()) {
        const QString text = m_write.lastError().text();
        if (error != nullptr)
            *error = AppError::fail(AppError::Db, QStringLiteral("数据库打开失败：%1").arg(text));
        close();
        return false;
    }

    // SQLite 外键是每连接设置；WAL 让读写双连接互不长期阻塞
    for (QSqlDatabase *conn : {&m_write, &m_read}) {
        QSqlQuery pragma(*conn);
        if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
            if (error != nullptr) {
                *error = AppError::fail(AppError::Db,
                                        QStringLiteral("启用外键失败：%1").arg(pragma.lastError().text()));
            }
            close();
            return false;
        }
    }
    {
        QSqlQuery pragma(m_write);
        pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    }

    m_path = databasePath;
    qInfo().noquote() << "[db] 已打开:" << QDir::toNativeSeparators(databasePath);
    return true;
}

void DbAccess::close()
{
    if (m_write.isOpen())
        m_write.close();
    if (m_read.isOpen())
        m_read.close();

    // 必须先清空连接句柄再 removeDatabase，否则 Qt 报警告且移除无效
    m_write = QSqlDatabase();
    m_read = QSqlDatabase();
    if (QSqlDatabase::contains(m_writeName))
        QSqlDatabase::removeDatabase(m_writeName);
    if (QSqlDatabase::contains(m_readName))
        QSqlDatabase::removeDatabase(m_readName);

    if (!m_path.isEmpty())
        qInfo().noquote() << "[db] 已关闭:" << QDir::toNativeSeparators(m_path);
    m_path.clear();
}

bool DbAccess::transaction(AppError *error)
{
    QSqlQuery query(m_write);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        fillError(error, AppError::Db, query);
        return false;
    }
    return true;
}

bool DbAccess::commit(AppError *error)
{
    QSqlQuery query(m_write);
    if (!query.exec(QStringLiteral("COMMIT"))) {
        fillError(error, AppError::Db, query);
        return false;
    }
    return true;
}

bool DbAccess::rollback(AppError *error)
{
    QSqlQuery query(m_write);
    if (!query.exec(QStringLiteral("ROLLBACK"))) {
        fillError(error, AppError::Db, query);
        return false;
    }
    return true;
}

void DbAccess::bind(QSqlQuery &query, const QVariantMap &params)
{
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        query.bindValue(QLatin1Char(':') + it.key(), it.value());
}

void DbAccess::fillError(AppError *error, AppError::Code code, const QSqlQuery &query)
{
    if (error != nullptr)
        *error = AppError::fail(code,
                                QStringLiteral("%1（SQL: %2）")
                                    .arg(query.lastError().text(), query.lastQuery()));
}

bool DbAccess::execute(const QString &sql, const QVariantMap &params, AppError *error)
{
    return execute(sql, params, nullptr, error);
}

bool DbAccess::execute(const QString &sql, const QVariantMap &params, qint64 *insertedId,
                       AppError *error)
{
    QSqlQuery query(m_write);
    if (!query.prepare(sql)) {
        fillError(error, AppError::Db, query);
        return false;
    }
    bind(query, params);
    if (!query.exec()) {
        fillError(error, AppError::Db, query);
        return false;
    }
    if (insertedId != nullptr)
        *insertedId = query.lastInsertId().toLongLong();
    return true;
}

QList<QVariantMap> DbAccess::select(const QString &sql, const QVariantMap &params,
                                    AppError *error) const
{
    QSqlQuery query(m_read);
    if (!query.prepare(sql)) {
        fillError(error, AppError::Db, query);
        return {};
    }
    bind(query, params);
    if (!query.exec()) {
        fillError(error, AppError::Db, query);
        return {};
    }

    const QSqlRecord record = query.record();
    QList<QVariantMap> rows;
    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < record.count(); ++i)
            row.insert(record.fieldName(i), query.value(i));
        rows.append(row);
    }
    return rows;
}

bool DbAccess::selectOne(const QString &sql, const QVariantMap &params,
                         QVariantMap &row, AppError *error) const
{
    const QList<QVariantMap> rows = select(sql, params, error);
    if (rows.isEmpty())
        return false;
    row = rows.first();
    return true;
}

QVariant DbAccess::scalar(const QString &sql, const QVariantMap &params, AppError *error) const
{
    QSqlQuery query(m_read);
    if (!query.prepare(sql)) {
        fillError(error, AppError::Db, query);
        return {};
    }
    bind(query, params);
    if (!query.exec()) {
        fillError(error, AppError::Db, query);
        return {};
    }
    if (!query.next())
        return {};
    return query.value(0);
}

bool DbAccess::executeRawStatements(const QString &sql, AppError *error)
{
    // 去整行注释后按 ';' 切分。当前 DDL 无触发器/存储过程，分号均为语句边界。
    QStringList lines;
    for (const QString &rawLine : sql.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("--")))
            continue;
        lines.append(rawLine);
    }

    const QStringList statements = lines.join(QLatin1Char('\n')).split(QLatin1Char(';'));
    for (const QString &statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty())
            continue;
        QSqlQuery query(m_write);
        if (!query.exec(trimmed)) {
            fillError(error, AppError::Db, query);
            return false;
        }
    }
    return true;
}
