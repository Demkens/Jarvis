#pragma once

#include <QList>
#include <QString>

#include "common/AppError.h"

class DbAccess;

// 创作者/别名服务（开发文档 3.4 / 6.6）。
// 纪律：
// - 按名查找 → 别名匹配 → find-or-create，正式名默认取第一次输入的名称；
// - 别名允许跨创作者重复，歧义时报错由用户裁决，不擅自挑一个；
// - id=0 为 anonymous（建库时预置），空名输入一律解析到它；
// - 写操作走 DbAccess 写连接，可参与调用方开启的事务（导入回滚时一并回滚）。
class CreatorService
{
public:
    enum Kind
    {
        Person = 1,   // 个人
        Group = 2,    // 团体
        Official = 3, // 官方
    };

    explicit CreatorService(DbAccess *db);

    // 解析创作者：空名 → 0；精确名命中 → 其 id；唯一别名命中 → 正式名 id；
    // 别名歧义 → Conflict 错误；都未命中 → 新建（kind 默认个人）。
    // 返回 -1 表示出错（error 已填充）。
    qint64 resolveOrCreate(const QString &name, int kind = Person, AppError *error = nullptr);

    // 精确匹配正式名；未命中返回 -1
    qint64 findByName(const QString &name, AppError *error = nullptr) const;

    // 别名匹配的全部候选 id（可能多个）
    QList<qint64> findByAlias(const QString &alias, AppError *error = nullptr) const;

    // 登记别名（幂等：重复登记不算错误）
    bool addAlias(qint64 creatorId, const QString &alias, AppError *error = nullptr);

    // kind 维护（1=个人 2=团体 3=官方）
    bool setKind(qint64 creatorId, int kind, AppError *error = nullptr);

    // 显示名；id=0 或未找到 → anonymous
    QString displayName(qint64 creatorId, AppError *error = nullptr) const;

private:
    DbAccess *m_db;
};
