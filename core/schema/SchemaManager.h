// Schema 管理器：
// - db 内 PRAGMA user_version 为权威版本；
// - 核心表迁移脚本内置、顺序执行、仅向前升级；
// - 类型包不再携带迁移脚本（见下），类型启用只登记 type 表一行。
// - 预发布期已做两次收束：
//   ① 迁移压缩（squash）：v1–v6 六段历史折叠为单一基线 v1；
//   ② 类型分表机制删除：类型无 DB 侧私有字段，`<form>` 分表与 migrate.sql 契约一并移除。
// - v2 为 M9 查重扩展：新增 photohash（图片内容/感知指纹）与 filmhash（视频指纹预留表）。
//   今后新增核心结构变更从 v3 起递增（在 ensureCoreSchema 中追加分支）。
// - 库版本高于内置版本时仅告警、不静默当作"已是最新"（见 ensureCoreSchema）。
// 业务代码中不允许散落 DDL。

#pragma once

#include "common/AppError.h"

class DbAccess;

class SchemaManager
{
public:
    // 当前内置核心表版本（v1 基线 + v2 查重扩展）
    static constexpr int kCoreSchemaVersion = 2;

    // 建库/打开库时调用：把核心表补齐到最新版本
    bool ensureCoreSchema(DbAccess &db, AppError *error = nullptr);
};
