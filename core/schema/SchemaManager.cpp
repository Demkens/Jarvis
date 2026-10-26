#include "SchemaManager.h"

#include <QDebug>
#include <QVariantMap>

#include "library/DbAccess.h"

namespace {

// 核心表基线（预发布期迁移压缩 / squash）：
// 本程序尚未发布正式版本，不存在需要支持的"旧库"，故把历史上按版本递增的 v1–v6 六段迁移
// 折叠为单一基线 v1：新建库从 0 直接执行本列表，即得到最终结构。
// 压缩删掉的是"迁移历史"，而非"迁移机制"——版本号、分段、门控、事务包裹全部保留，
// 今后新增结构变更从 v2 起递增即可（在 ensureCoreSchema 中追加分支）。
//
// 与折叠前的差异（被省略的语句都是"空库上恒为空操作"）：
// - v4 / v5 的两条 ALTER TABLE ADD COLUMN → 两列直接内联进 file 的建表语句；
// - v5 按各类型分表动态回填 creation_date 的 SQL → 无存量行可回填；
// - v3 为存量无创作者文件补「佚名」关联 → 无存量行；
//   仅保留种子数据"creator id=0 即「佚名」"（无创作者统一兜底到它）。
//
// 收束（2026-10-17 二次）：类型分表机制删除——type 表不再有 schema_version 列，
// 类型不再有 DB 侧私有字段（创作者→file_creator、创作日期→file.creation_date、
// 浏览位置→browse_state，均已收归核心），故每个类型不再建 <form> 分表。
//
// 建表顺序须满足外键引用：type / creator 在前，alias 与 file 随后，关联表与 tag 最后。
const QStringList kCoreBaselineStatements = {
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS type (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE,
            form TEXT    NOT NULL UNIQUE
        ))"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS creator (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE
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
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            title         TEXT    NOT NULL DEFAULT 'untitled',
            type_id       INTEGER NOT NULL,
            score         INTEGER NOT NULL DEFAULT 0 CHECK (score >= 0 AND score <= 100),
            size          INTEGER NOT NULL DEFAULT 0,
            storage_path  INTEGER NOT NULL,
            aspect_ratio  REAL    NOT NULL DEFAULT 0,
            creation_date TEXT    NOT NULL DEFAULT '',
            created_time  TEXT    NOT NULL,
            updated_time  TEXT    NOT NULL,
            FOREIGN KEY (type_id) REFERENCES type(id)
        ))"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS browse_state (
            file_id      INTEGER PRIMARY KEY,
            position     TEXT NOT NULL DEFAULT '',
            updated_time TEXT NOT NULL,
            FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE CASCADE
        ))"),
    // id=0 统一为「佚名」：无创作者的数据包一律兜底到它
    QStringLiteral("INSERT OR IGNORE INTO creator (id, name) VALUES (0, '佚名')"),
    // 创作者多对多关联表：sort_order=0 为主要创作者，1、2… 为次要；file_id 级联删一并清理
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS file_creator (
            file_id    INTEGER NOT NULL,
            creator_id INTEGER NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (file_id, creator_id),
            FOREIGN KEY (file_id)    REFERENCES file(id)    ON DELETE CASCADE,
            FOREIGN KEY (creator_id) REFERENCES creator(id)
        ))"),
    // 按创作者反查数据包（筛选/统计）时避免全表扫描
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_creator_creator ON file_creator(creator_id)"),
    // 标签树：顶级标签 parent_id 为 NULL；因 SQLite 中 NULL != NULL，一条索引无法同时约束
    // 「顶级同名」与「同层同名」，故拆成两条部分唯一索引分别覆盖两种情形
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
    // 按标签反查数据包（筛选/统计）时避免全表扫描
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_tag_tag ON file_tag(tag_id)"),
};

// v2：M9 查重扩展。
// - photohash 以 (文件 id, 包内相对路径) 为主键，一个数据包对应多行；
//   导入时写入 content_hash / size_bytes / aHash / pHash / dHash，历史数据由「补指纹」补齐；
// - filmhash 仅建表预留，本期不写入数据，视频指纹格式后续以 JSON 约定；
// - content/size 建索引用于「完全重复」点查与文件大小预筛。
const QStringList kSchemaV2Statements = {
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS photohash (
            id            INTEGER NOT NULL,
            relative_path TEXT    NOT NULL,
            content_hash  TEXT,
            size_bytes    INTEGER,
            ahash         TEXT,
            phash         TEXT,
            dhash         TEXT,
            algo_version  INTEGER NOT NULL DEFAULT 1,
            PRIMARY KEY (id, relative_path),
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE
        ))"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_photohash_content ON photohash(content_hash)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_photohash_size ON photohash(size_bytes)"),
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS filmhash (
            id                INTEGER NOT NULL,
            relative_path     TEXT    NOT NULL,
            video_fingerprint TEXT,
            PRIMARY KEY (id, relative_path),
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE
        ))"),
};

// v3：M8a 短笔记扩展。
// 只存笔记元数据；正文由 NoteService 以 Markdown 写入 envs/<库名>/note/<id>.md，
// 库内不存正文，避免文件与数据库两份内容互相覆盖。
// 笔记与数据包的关联（file_note）延后到 M8b，届时另起 v4 迁移。
const QStringList kSchemaV3Statements = {
    QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS note (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            title        TEXT    NOT NULL DEFAULT '',
            created_time TEXT    NOT NULL,
            updated_time TEXT    NOT NULL
        ))"),
};

} // namespace

bool SchemaManager::ensureCoreSchema(DbAccess &db, AppError *error)
{
    const int currentVersion = db.scalar(QStringLiteral("PRAGMA user_version")).toInt();

    // 库版本超前（可能由更新版本的程序创建）：仅告警，不静默当作"已是最新"，也不阻断启动
    if (currentVersion > kCoreSchemaVersion) {
        qWarning().noquote()
            << QStringLiteral("[schema] 库版本 %1 高于程序内置版本 %2（可能由更新版本创建），"
                              "跳过核心表迁移")
                   .arg(currentVersion).arg(kCoreSchemaVersion);
        return true;
    }

    if (currentVersion >= kCoreSchemaVersion)
        return true;

    if (!db.transaction(error))
        return false;

    for (int version = currentVersion + 1; version <= kCoreSchemaVersion; ++version) {
        const QStringList *statements = nullptr;
        if (version == 1)
            statements = &kCoreBaselineStatements;
        else if (version == 2)
            statements = &kSchemaV2Statements;
        else if (version == 3)
            statements = &kSchemaV3Statements;
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
