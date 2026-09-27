#pragma once

#include "common/AppError.h"

class DbAccess;
struct TypePackage;

// Schema 管理器：
// - db 内 PRAGMA user_version 为权威版本；
// - 核心表迁移脚本内置、顺序执行、仅向前升级；
// - 类型分表由类型包 migrate.sql 描述，以 "-- @version N" 分段，事务内执行。
// 业务代码中不允许散落 DDL。
class SchemaManager
{
public:
    // 当前内置核心表版本
    static constexpr int kCoreSchemaVersion = 1;

    // 建库/打开库时调用：把核心表补齐到最新版本
    bool ensureCoreSchema(DbAccess &db, AppError *error = nullptr);

    // 为某库启用一个类型：执行其 migrate.sql 全部分段并登记 type 表。
    // M1 仅在建库时调用（新库，从版本 0 起步）；增量升级在后续里程碑补充。
    bool enableTypePackage(DbAccess &db, const TypePackage &package, AppError *error = nullptr);

private:
    // 把 migrate.sql 解析为按版本号升序的 (version, sql) 段列表
    static bool parseMigrateScript(const QString &content,
                                   QList<QPair<int, QString>> &segments,
                                   AppError *error = nullptr);
};
