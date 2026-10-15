-- draw 类型分表迁移脚本
-- 约定：以 "-- @version N" 分段（N 从 1 起单调递增）；
-- SchemaManager 依据 type.schema_version 只补执行缺失段落（版本门控）。

-- @version 1
CREATE TABLE IF NOT EXISTS draw (
    id            INTEGER PRIMARY KEY,
    creator_id    INTEGER NOT NULL DEFAULT 0,
    creation_date TEXT,
    FOREIGN KEY (id)         REFERENCES file(id)    ON DELETE CASCADE,
    FOREIGN KEY (creator_id) REFERENCES creator(id)
);

-- @version 2
-- 创作者改多对多：核心 v2 已建 file_creator 关联表，draw 不再持有 creator_id。
-- 1) 存量 creator_id 迁入关联表（0=anonymous 不迁，显示层兜底为 anonymous）
INSERT INTO file_creator (file_id, creator_id, sort_order)
    SELECT id, creator_id, 0 FROM draw
    WHERE creator_id IS NOT NULL AND creator_id <> 0;
-- 2) 整表重建去掉 creator_id（SQLite 禁止 DROP 被外键引用的列）；
--    无任何表引用 draw，重建安全；FK 校验（id 在 file 中）在原行上本就成立
ALTER TABLE draw RENAME TO draw_old;
CREATE TABLE draw (
    id            INTEGER PRIMARY KEY,
    creation_date TEXT,
    FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE
);
INSERT INTO draw (id, creation_date) SELECT id, creation_date FROM draw_old;
DROP TABLE draw_old;

-- @version 3
-- 创作日期升格为核心字段：核心 v5 已新增 file.creation_date 并从本表 creation_date 回填存量
-- （库打开顺序为「核心迁移 → 类型迁移」，故此处重建时回填已完成）。
-- 创作者已收归 file_creator、创作日期已收归 file，本表不再持有任何类型自有字段，
-- 仅保留 id 主键作为「类型标记」（分表行按需建立；核心读取按「缺行即无类型字段」容忍）。
-- SQLite 禁止直接 DROP 被外键引用的列，故整表重建。
ALTER TABLE draw RENAME TO draw_old;
CREATE TABLE draw (
    id INTEGER PRIMARY KEY,
    FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE
);
INSERT INTO draw (id) SELECT id FROM draw_old;
DROP TABLE draw_old;
