-- draw 类型分表迁移脚本
-- 约定：以 "-- @version N" 分段（N 从 1 起单调递增）；
-- SchemaManager 依据 type.schema_version 在事务内依次补执行缺失段落；
-- 各段须可重复执行（使用 IF NOT EXISTS）。

-- @version 1
CREATE TABLE IF NOT EXISTS draw (
    id            INTEGER PRIMARY KEY,
    creator_id    INTEGER NOT NULL DEFAULT 0,
    creation_date TEXT,
    FOREIGN KEY (id)         REFERENCES file(id)    ON DELETE CASCADE,
    FOREIGN KEY (creator_id) REFERENCES creator(id)
);
