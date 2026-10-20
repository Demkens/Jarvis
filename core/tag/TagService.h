#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

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

    // ---- 标签树导入/导出（JSON 文件；仅名称与层级，不含与数据包的绑定关系）----
    // 导出当前标签树到 dir 下的 tags_yyyyMMdd_HHmmss.json（嵌套结构，见 importFromJson 格式）。
    // dir 为空返回 Validation；目录不存在则创建。成功时 outPath（可空）输出实际文件路径。
    bool exportToJson(const QString &dir, QString *outPath, AppError *error = nullptr) const;

    // 从 JSON 文件导入标签树并合并进现有体系：同层同名复用已有标签，其子标签并入其下（幂等，可重复导入）。
    // 事务执行，任一节点失败整体回滚；成功时 created/merged（可空）输出新建/复用计数。
    // 文件不存在或格式非法返回 Validation，不写库。
    bool importFromJson(const QString &path, int *created, int *merged, AppError *error = nullptr);

private:
    // 标签是否存在
    bool tagExists(qint64 tagId, AppError *error = nullptr) const;
    // 逐级向上收集祖先 id（不含自身），顺序为由近及远
    QList<qint64> ancestorIds(qint64 tagId, AppError *error = nullptr) const;
    // 递归导入单个节点（node 形如 {name, children:[...]}）；parentId=0 为顶级。
    // existing 为「父 id + 名称 → 标签 id」的同层同名索引：导入前预载现有标签，导入中新建的节点即时登记，
    // 从而在同一事务内（写连接未提交、读连接不可见的窗口内）也能正确去重。
    bool importNode(const QVariantMap &node, qint64 parentId, int *created, int *merged,
                    QHash<QString, qint64> &existing, AppError *error = nullptr);

    DbAccess *m_db;
};
