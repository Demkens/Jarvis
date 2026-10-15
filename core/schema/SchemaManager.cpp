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

// 核心 v2：创作者多对多关联表（替代各类型分表上的 creator_id 单值列）。
// sort_order=0 为主要创作者，1、2… 为次要；file_id 级联删保证删除数据包时一并清理。
const QStringList kCoreV2Statements = {
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS file_creator (
            file_id    INTEGER NOT NULL,
            creator_id INTEGER NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (file_id, creator_id),
            FOREIGN KEY (file_id)    REFERENCES file(id)    ON DELETE CASCADE,
            FOREIGN KEY (creator_id) REFERENCES creator(id)
        ))"),
    // 按创作者反查数据包（筛选/统计二期用）时避免全表扫描
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_creator_creator ON file_creator(creator_id)"),
};

// 核心 v3：无创作者统一为「佚名」——保证该创作者存在，并为存量"无关联行"的文件补上关联，
// 使「佚名」可被检索，且显示/搜索语义一致（开发文档 6.6 的 anonymous 语义落地为可检索实体）。
const QStringList kCoreV3Statements = {
    // 1) 若无同名「佚名」，把预置 id=0 的 anonymous 更名为「佚名」（保持 id 稳定）
    QStringLiteral("UPDATE creator SET name = '佚名' "
                   "WHERE id = 0 AND name = 'anonymous' "
                   "AND NOT EXISTS (SELECT 1 FROM creator WHERE name = '佚名')"),
    // 2) 兜底：确保「佚名」存在（id=0 已被占用或缺失的情形）
    QStringLiteral("INSERT OR IGNORE INTO creator (name, kind) VALUES ('佚名', 1)"),
    // 3) 为存量无创作者文件补「佚名」关联（sort_order=0 即主要创作者）
    QStringLiteral("INSERT INTO file_creator (file_id, creator_id, sort_order) "
                   "SELECT f.id, (SELECT id FROM creator WHERE name = '佚名'), 0 FROM file f "
                   "WHERE NOT EXISTS (SELECT 1 FROM file_creator fc WHERE fc.file_id = f.id)"),
};

// 核心 v4：封面长宽比入库（瀑布流布局用，= 宽/高；0 视为未知）。
// 封面由核心等比生成，其 AR 等于原图 AR；存量行在打开库时按封面文件头补全。
const QStringList kCoreV4Statements = {
    QStringLiteral("ALTER TABLE file ADD COLUMN aspect_ratio REAL NOT NULL DEFAULT 0"),
};

// 核心 v5：创作日期升格为核心字段 file.creation_date（约定变更）
// 背景：早期按「类型定性数据」把它放在各类型分表的同名列，导致跨类型按创作日期筛选
// 必须动态拼分表子查询、且精度（YYYY / YYYY-MM / YYYY-MM-DD）与可空性不统一；
// 现统一收归 file 表一列，精度固定 YYYY-MM-DD；分表旧列由各类型包 migrate.sql 自行清理。
const QStringList kCoreV5Statements = {
    QStringLiteral("ALTER TABLE file ADD COLUMN creation_date TEXT NOT NULL DEFAULT ''"),
};

// 核心 v6：标签体系（开发文档 6.7）——树状单父标签表 tag + 包级多对多关联表 file_tag。
// 顶级标签 parent_id 为 NULL；因 SQLite 中 NULL != NULL，一个索引无法同时约束
// 「顶级同名」与「同层同名」，故拆成两条部分唯一索引分别覆盖两种情形。
// 说明：本列表中 STATEMENT 内不得出现行内注释（executeRawStatements 按 ';' 切分）。
const QStringList kCoreV6Statements = {
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS tag (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            name      TEXT    NOT NULL,
            parent_id INTEGER,
            CHECK (parent_id IS NULL OR parent_id != id),
            FOREIGN KEY (parent_id) REFERENCES tag(id) ON DELETE CASCADE
        ))"),
    QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_tag_root_name "
                   "ON tag(name) WHERE parent_id IS NULL"),
    QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_tag_child_name "
                   "ON tag(parent_id, name) WHERE parent_id IS NOT NULL"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS file_tag (
            file_id INTEGER NOT NULL,
            tag_id  INTEGER NOT NULL,
            PRIMARY KEY (file_id, tag_id),
            FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE CASCADE,
            FOREIGN KEY (tag_id)  REFERENCES tag(id)  ON DELETE CASCADE
        ))"),
    // 按标签反查数据包（筛选/统计用）时避免全表扫描
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_tag_tag ON file_tag(tag_id)"),
};

} // namespace

// 拼进 SQL 的表名只接受安全标识符；与 StorageService::isSafeFormName 同规则，
// 此处独立实现以免 schema 层反向依赖 storage 层。
static bool isSafeIdentifier(const QString &name)
{
    if (name.isEmpty() || name.size() > 64)
        return false;
    if (!name.at(0).isLower() || !name.at(0).isLetter())
        return false;
    for (const QChar ch : name) {
        if (ch.isLetter() && !ch.isLower())
            return false;
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
            return false;
    }
    return true;
}

bool SchemaManager::applyCoreV5(DbAccess &db, AppError *error)
{
    // 1) 加列（默认空串：存量行在下面第 2、3 步回填）
    for (const QString &statement : kCoreV5Statements) {
        if (!db.executeRawStatements(statement, error))
            return false;
    }

    // 2) 分表回填：早期创作日期住在各类型分表的 creation_date 列，精度不一，
    //    统一归一到 YYYY-MM-DD（年补 -01-01，月补 -01）
    AppError local;
    const QList<QVariantMap> typeRows =
        db.select(QStringLiteral("SELECT form FROM type"), {}, &local);
    if (!local.ok()) {
        if (error != nullptr)
            *error = local;
        return false;
    }

    for (const QVariantMap &typeRow : typeRows) {
        const QString form = typeRow.value(QStringLiteral("form")).toString();
        if (!isSafeIdentifier(form))
            continue;

        const int exists =
            db.scalar(QStringLiteral("SELECT count(*) FROM sqlite_master "
                                     "WHERE type = 'table' AND name = :n"),
                      {{QStringLiteral("n"), form}}, &local)
                .toInt();
        if (!local.ok()) {
            if (error != nullptr)
                *error = local;
            return false;
        }
        if (exists != 1)
            continue;

        // 分表未必含同名列（类型包可能已自行清理或本就没有）
        bool hasDateColumn = false;
        const QList<QVariantMap> columns =
            db.select(QStringLiteral("PRAGMA table_info(%1)").arg(form), {}, &local);
        if (!local.ok()) {
            if (error != nullptr)
                *error = local;
            return false;
        }
        for (const QVariantMap &column : columns) {
            if (column.value(QStringLiteral("name")).toString() == QStringLiteral("creation_date"))
                hasDateColumn = true;
        }
        if (!hasDateColumn)
            continue;

        const QString sql =
            QStringLiteral("UPDATE file SET creation_date = "
                           "(SELECT CASE length(d.creation_date) "
                           "         WHEN 4 THEN d.creation_date || '-01-01' "
                           "         WHEN 7 THEN d.creation_date || '-01' "
                           "         ELSE d.creation_date END "
                           " FROM %1 d WHERE d.id = file.id) "
                           "WHERE creation_date = '' AND id IN "
                           "(SELECT id FROM %1 WHERE creation_date IS NOT NULL AND creation_date <> '')")
                .arg(form);
        if (!db.execute(sql, {}, error))
            return false;
    }

    // 3) 兜底：分表无同名列、或分表日期为空的行，用入库日期（created_time 的日期部分）
    return db.execute(QStringLiteral("UPDATE file SET creation_date = substr(created_time, 1, 10) "
                                     "WHERE creation_date = ''"),
                      {}, error);
}

bool SchemaManager::ensureCoreSchema(DbAccess &db, AppError *error)
{
    const int currentVersion = db.scalar(QStringLiteral("PRAGMA user_version")).toInt();
    if (currentVersion >= kCoreSchemaVersion)
        return true;

    if (!db.transaction(error))
        return false;

    for (int version = currentVersion + 1; version <= kCoreSchemaVersion; ++version) {
        if (version == 5) {
            // v5 需按 type 表登记的分表名动态拼接，无法写成静态语句列表，改由代码迁移
            if (!applyCoreV5(db, error)) {
                db.rollback();
                return false;
            }
        } else {
            const QStringList *statements = nullptr;
            if (version == 1)
                statements = &kCoreV1Statements;
            else if (version == 2)
                statements = &kCoreV2Statements;
            else if (version == 3)
                statements = &kCoreV3Statements;
            else if (version == 4)
                statements = &kCoreV4Statements;
            else if (version == 6)
                statements = &kCoreV6Statements;

            // 未登记的版本：不推进 user_version（避免静默跳过迁移），下次启动重试
            if (statements == nullptr)
                continue;

            for (const QString &statement : *statements) {
                if (!db.executeRawStatements(statement, error)) {
                    db.rollback();
                    return false;
                }
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

    // 已应用版本：读 type 表登记值；未登记（新库首启）视为 0 → 执行全部分段
    AppError local;
    const QVariant appliedVariant = db.scalar(
        QStringLiteral("SELECT schema_version FROM type WHERE form = :form"),
        {{QStringLiteral("form"), package.form}}, &local);
    if (!local.ok()) {
        if (error != nullptr)
            *error = local;
        return false;
    }
    const int applied = appliedVariant.toInt(); // 无行时 QVariant 无效 → 0

    if (!db.transaction(error))
        return false;

    for (const auto &[version, body] : segments) {
        if (version <= applied)
            continue; // 已应用段落跳过（老库打开时只补缺失段）
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
