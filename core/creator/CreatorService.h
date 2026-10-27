#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "common/AppError.h"

class DbAccess;

// 创作者管理视图行（M11a）：正式名、关联包数、别名列表。
struct CreatorSummary
{
    qint64 id = -1;
    QString name;
    int packageCount = 0;
    QStringList aliases;
};

// 创作者/别名服务（开发文档 3.4 / 6.6）。
// 纪律：
// - 按名查找 → 别名匹配 → find-or-create，正式名默认取第一次输入的名称；
// - 别名允许跨创作者重复，歧义时报错由用户裁决，不擅自挑一个；
// - 空名输入一律解析到默认创作者「佚名」（建库/迁移时保证存在）；
// - 写操作走 DbAccess 写连接，可参与调用方开启的事务（导入回滚时一并回滚）；
// - 「佚名」为系统保留创作者（id=0），不可重命名、不接纳别名。
class CreatorService
{
public:
    explicit CreatorService(DbAccess *db);

    // 函数: 列出创作者及其别名与关联包数（M11a 管理界面用）
    // 输入: keyword —— 搜索词，同时匹配正式名与别名（空=全部，子串匹配）
    // 输出: 按正式名排序的创作者列表；别名按名称排序；查询失败返回空列表并填充 error
    QList<CreatorSummary> listCreators(const QString &keyword, AppError *error = nullptr) const;

    // 函数: 重命名创作者的正式名
    // 输入: creatorId —— 目标创作者；newName —— 新正式名（trim 后使用）
    // 输出: 成功 true；失败 false 并填充 error（空名/含分隔符/与已有正式名冲突/
    //       目标不存在/目标是「佚名」/名称被保留）
    bool renameCreator(qint64 creatorId, const QString &newName, AppError *error = nullptr);

    // 函数: 移除某个创作者的一条别名
    // 输入: creatorId —— 创作者；alias —— 要移除的别名（trim 后使用）
    // 输出: 成功 true（别名不存在也视为成功）；空别名/非法 id 返回 false
    bool removeAlias(qint64 creatorId, const QString &alias, AppError *error = nullptr);

    // 解析创作者：空名 → 默认创作者「佚名」；精确名命中 → 其 id；唯一别名命中 → 正式名 id；
    // 别名歧义 → Conflict 错误；都未命中 → 新建。
    // 返回 -1 表示出错（error 已填充）。
    qint64 resolveOrCreate(const QString &name, AppError *error = nullptr);

    // 无创作者时的默认显示名（预置创作者「佚名」）
    static QString anonymousName();

    // 精确匹配正式名；未命中返回 -1
    qint64 findByName(const QString &name, AppError *error = nullptr) const;

    // 别名匹配的全部候选 id（可能多个）
    QList<qint64> findByAlias(const QString &alias, AppError *error = nullptr) const;

    // 登记别名（幂等：重复登记不算错误）
    bool addAlias(qint64 creatorId, const QString &alias, AppError *error = nullptr);

    // 显示名；未找到（含 id<=0）→ 「佚名」兜底
    QString displayName(qint64 creatorId, AppError *error = nullptr) const;

    // 把用户输入的多创作者文本拆分为名字列表（支持中英文逗号/分号/顿号/竖线/换行分隔），
    // 逐项 trim 并丢弃空项；返回顺序即 sort_order 语义（第一个为主要创作者）。
    static QStringList splitNames(const QString &text);

private:
    DbAccess *m_db;
};
