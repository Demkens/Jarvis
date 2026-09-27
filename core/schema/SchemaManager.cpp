#include "SchemaManager.h"

#include <QFile>
#include <QRegularExpression>
#include <QVariantMap>

#include "library/DbAccess.h"
#include "types/TypePackage.h"

namespace {

// 建表顺序须满足外键引用：type / creator 在前，alias 与 file 随后，browse_state 最后。
const QStringList kCoreV1Statements = {
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS type (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            name           TEXT    NOT NULL UNIQUE,
            form           TEXT    NOT NULL UNIQUE,
            schema_version INTEGER NOT NULL DEFAULT 1
        ))"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS creator (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE,
            kind INTEGER NOT NULL DEFAULT 1
        ))"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS alias (
            id    INTEGER NOT NULL,
            alias TEXT    NOT NULL,
            PRIMARY KEY (id, alias),
            FOREIGN KEY (id) REFERENCES creator(id) ON DELETE CASCADE
        ))"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS file (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            title        TEXT    NOT NULL DEFAULT 'untitled',
            type_id      INTEGER NOT NULL,
            score        INTEGER NOT NULL DEFAULT 0 CHECK (score >= 0 AND score <= 100),
            size         INTEGER NOT NULL DEFAULT 0,
            storage_path INTEGER NOT NULL,
            created_time TEXT    NOT NULL,
            updated_time TEXT    NOT NULL,
            FOREIGN KEY (type_id) REFERENCES type(id)
        ))"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS browse_state (
            file_id      INTEGER PRIMARY KEY,
            position     TEXT NOT NULL DEFAULT '',
            updated_time TEXT NOT NULL,
            FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE CASCADE
        ))"),
    // id=0 保留为 anonymous（开发文档 6.6）
    QStringLiteral("INSERT OR IGNORE INTO creator (id, name, kind) VALUES (0, 'anonymous', 1)"),
};

} // namespace

bool SchemaManager::ensureCoreSchema(DbAccess &db, AppError *error)
{
    const int currentVersion = db.scalar(QStringLiteral("PRAGMA user_version")).toInt();
    if (currentVersion >= kCoreSchemaVersion)
        return true;

    if (!db.transaction(error))
        return false;

    for (int version = currentVersion + 1; version <= kCoreSchemaVersion; ++version) {
        const QStringList *statements = nullptr;
        if (version == 1)
            statements = &kCoreV1Statements;

        if (statements == nullptr)
            continue;

        for (const QString &statement : *statements) {
            if (!db.executeRawStatements(statement, error)) {
                db.rollback();
                return false;
            }
        }
        if (!db.execute(QStringLiteral("PRAGMA user_version = %1").arg(version), {}, error)) {
            db.rollback();
            return false;
        }
    }

    return db.commit(error);
}

bool SchemaManager::parseMigrateScript(const QString &content,
                                       QList<QPair<int, QString>> &segments,
                                       AppError *error)
{
    static const QRegularExpression marker(QStringLiteral(R"(^--\s*@version\s+(\d+)\s*$)"));

    segments.clear();
    int currentVersion = 0;
    QStringList currentBody;

    auto flush = [&]() {
        if (currentVersion > 0)
            segments.append({currentVersion, currentBody.join(QLatin1Char('\n'))});
        currentBody.clear();
    };

    for (const QString &line : content.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = marker.match(line.trimmed());
        if (match.hasMatch()) {
            flush();
            currentVersion = match.captured(1).toInt();
            continue;
        }
        if (currentVersion > 0)
            currentBody.append(line);
    }
    flush();

    if (segments.isEmpty()) {
        if (error != nullptr) {
            *error = AppError::fail(AppError::Db,
                                    QStringLiteral("migrate.sql 缺少 '-- @version N' 分段标记"));
        }
        return false;
    }

    int expected = 1;
    for (const auto &[version, body] : segments) {
        if (version != expected) {
            if (error != nullptr) {
                *error = AppError::fail(AppError::Db,
                                        QStringLiteral("migrate.sql 版本段不连续：期望 %1，实际 %2")
                                            .arg(expected).arg(version));
            }
            return false;
        }
        if (body.trimmed().isEmpty()) {
            if (error != nullptr) {
                *error = AppError::fail(AppError::Db,
                                        QStringLiteral("migrate.sql 版本段 %1 内容为空").arg(version));
            }
            return false;
        }
        ++expected;
    }
    return true;
}

bool SchemaManager::enableTypePackage(DbAccess &db, const TypePackage &package, AppError *error)
{
    QFile file(package.migratePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = AppError::fail(AppError::Io,
                                    QStringLiteral("无法打开类型迁移脚本：%1").arg(package.migratePath));
        }
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QList<QPair<int, QString>> segments;
    if (!parseMigrateScript(content, segments, error))
        return false;

    if (!db.transaction(error))
        return false;

    for (const auto &[version, body] : segments) {
        if (!db.executeRawStatements(body, error)) {
            db.rollback();
            return false;
        }
    }

    // 登记 type 表；同 form 重复启用时更新版本（支持以后补升级）
    const QVariantMap params = {
        {QStringLiteral("name"), package.displayName},
        {QStringLiteral("form"), package.form},
        {QStringLiteral("schema_version"), segments.last().first},
    };
    if (!db.execute(
            QStringLiteral(
                "INSERT INTO type (name, form, schema_version) VALUES (:name, :form, :schema_version) "
                "ON CONFLICT(form) DO UPDATE SET name = excluded.name, "
                "schema_version = excluded.schema_version"),
            params, error)) {
        db.rollback();
        return false;
    }

    return db.commit(error);
}
