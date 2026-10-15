#pragma once

#include "common/AppError.h"

class DbAccess;
struct TypePackage;

// Schema 管理器：
// - db 内 PRAGMA user_version 为权威版本；
// - 核心表迁移脚本内置、顺序执行、仅向前升级；
// - 类型分表由类型包 migrate.sql 描述，以 "-- @version N" 分段，事务内执行；
// - 类型包迁移按 type.schema_version 门控：只执行版本号大于已应用值的段落。
// 业务代码中不允许散落 DDL。
class SchemaManager
{
public:
    // 当前内置核心表版本
    static constexpr int kCoreSchemaVersion = 6;

    // 建库/打开库时调用：把核心表补齐到最新版本
    bool ensureCoreSchema(DbAccess &db, AppError *error = nullptr);

    // 为某库启用/升级一个类型：执行 migrate.sql 中版本号大于已应用值的段落并登记 type 表。
    // 建库时从 0 起步执行全部分段；打开既有库时按 type.schema_version 只补缺失段落（老库增量升级）。
    bool enableTypePackage(DbAccess &db, const TypePackage &package, AppError *error = nullptr);

private:
    // 核心 v5 迁移：创作日期升格为核心字段 file.creation_date。
    // 输入：已打开且处于事务中的库连接 db；输出：成功返回 true，失败置 error 由调用方回滚。
    // 三步：加列 → 从各类型分表同名列回填（精度归一为 YYYY-MM-DD）→ 余下空值以入库日期兜底。
    // 因分表名来自 type 表登记（动态），无法写成静态语句列表，故单独以代码实现。
    static bool applyCoreV5(DbAccess &db, AppError *error = nullptr);

    // 把 migrate.sql 解析为按版本号升序的 (version, sql) 段列表
    static bool parseMigrateScript(const QString &content,
                                   QList<QPair<int, QString>> &segments,
                                   AppError *error = nullptr);
};
