#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

#include "common/AppError.h"

class DbAccess;

// 标签服务（开发文档 6.7；M6）：
// 提供「全库共享、与类型无关」的树状标签（森林，顶级可多根）及其与数据包的多对多绑定。
// 纪律：
// - 标签必须先在标签管理中创建才能使用；同层重名拒绝（顶级与子级分别由部分唯一索引兜底）；
// - 某包的标签集合恒保持反链：任意两标签不得构成祖先—后代关系；非法集合一律拒绝并报错，
//   不静默规范化（交互层的自动调整是 UI 职责，服务层只做校验）；
// - 删除标签级联删除其全部子孙与 file_tag 关联行（靠外键 ON DELETE CASCADE）；
// - 本服务不持有事件总线：结构性/绑定变更后的刷新由门面（TypeEngine）负责广播。
class TagService
{
public:
    explicit TagService(DbAccess *db);

    // 标签树：扁平数组，DFS 先序，每项 {id, parent_id, name, depth}。
    // parent_id 为 0 表示顶级（SQL 中为 NULL）；同层按名称字典序（不区分大小写）。
    // 输出交给 QML 按 depth 缩进渲染（不用递归组件）。失败返回空表并置 error。
    QVariantList tree(AppError *error = nullptr) const;

    // 新建标签：parentId = 0 表示顶级，> 0 必须指向已存在标签。
    // 成功时 newId（可空）输出新 id；同层重名返回 Conflict，父标签不存在返回 Validation。
    bool createTag(qint64 parentId, const QString &name, qint64 *newId = nullptr,
                   AppError *error = nullptr);

    // 重命名标签（不改变层级）：同层重名返回 Conflict，标签不存在返回 Validation。
    bool renameTag(qint64 tagId, const QString &name, AppError *error = nullptr);

    // 删除标签：其子孙与 file_tag 关联行级联删除；标签不存在返回 Validation。
    bool deleteTag(qint64 tagId, AppError *error = nullptr);

    // 某数据包已绑定的标签 id 列表（升序）
    QList<qint64> tagsOfFile(qint64 fileId, AppError *error = nullptr) const;

    // 整体替换某数据包的标签集合（事务内先删后插）。
    // 前置校验：数据包存在性 + 各标签存在性 + 反链（任意两标签不得互为祖先/后代），
    // 任一违反即拒绝并置 error。tagIds 为空表示清空该包全部标签。
    bool setFileTags(qint64 fileId, const QList<qint64> &tagIds, AppError *error = nullptr);

    // 某标签的全部子孙 id（含自身）。用于筛选语义：选中标签即命中其整棵子树。
    QList<qint64> subtreeIds(qint64 tagId, AppError *error = nullptr) const;

    // 单标签显示名；不存在（含 id <= 0）返回空串
    QString nameOf(qint64 tagId, AppError *error = nullptr) const;

private:
    // 标签是否存在
    bool tagExists(qint64 tagId, AppError *error = nullptr) const;
    // 逐级向上收集祖先 id（不含自身），顺序为由近及远
    QList<qint64> ancestorIds(qint64 tagId, AppError *error = nullptr) const;

    DbAccess *m_db;
};
