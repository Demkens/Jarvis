#include "TagService.h"

#include <algorithm>
#include <functional>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QVariantMap>

#include "library/DbAccess.h"

namespace {

AppError failValidation(const QString &message)
{
    return AppError::fail(AppError::Validation, message);
}

AppError failConflict(const QString &message)
{
    return AppError::fail(AppError::Conflict, message);
}

// 把局部错误转述给调用方（error 可空）
bool failed(const AppError &local, AppError *out)
{
    if (local.ok())
        return false;
    if (out != nullptr)
        *out = local;
    return true;
}

// 导出序列化：按「父 id → 子行列表」的分组递归构造嵌套 JSON 数组（结构与层级，不含绑定）
QJsonArray buildNodes(const QHash<qint64, QList<QVariantMap>> &children, qint64 parentId)
{
    QJsonArray array;
    const QList<QVariantMap> kids = children.value(parentId);
    for (const QVariantMap &kid : kids) {
        QJsonObject node;
        node.insert(QStringLiteral("name"), kid.value(QStringLiteral("name")).toString());
        node.insert(QStringLiteral("children"),
                    buildNodes(children, kid.value(QStringLiteral("id")).toLongLong()));
        array.append(node);
    }
    return array;
}

// 导入前置校验：整棵节点树须为「对象 + 非空 name + 可选的数组 children」，非法即整体拒绝（不写库）
bool validateTagNodes(const QJsonArray &nodes, AppError *error)
{
    for (const QJsonValue &value : nodes) {
        if (!value.isObject()) {
            if (error != nullptr)
                *error = failValidation(QStringLiteral("标签文件格式非法：节点须为对象"));
            return false;
        }
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
            if (error != nullptr)
                *error = failValidation(QStringLiteral("标签文件格式非法：存在名称为空的标签"));
            return false;
        }
        const QJsonValue kids = node.value(QStringLiteral("children"));
        if (!kids.isUndefined() && !kids.isArray()) {
            if (error != nullptr)
                *error = failValidation(QStringLiteral("标签文件格式非法：children 须为数组"));
            return false;
        }
        if (kids.isArray() && !validateTagNodes(kids.toArray(), error))
            return false;
    }
    return true;
}

// 同层同名索引键：父 id + 名称（parentId 为数字前缀，不存在歧义）
QString taggedKey(qint64 parentId, const QString &name)
{
    return QString::number(parentId) + QLatin1Char('|') + name;
}

} // namespace

TagService::TagService(DbAccess *db)
    : m_db(db)
{
}

bool TagService::tagExists(qint64 tagId, AppError *error) const
{
    if (tagId <= 0) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("标签不存在：#%1").arg(tagId));
        return false;
    }
    const QVariant count = m_db->scalar(QStringLiteral("SELECT count(*) FROM tag WHERE id = :id"),
                                        {{QStringLiteral("id"), tagId}}, error);
    if (error != nullptr && !error->ok())
        return false;
    return count.toInt() == 1;
}

QVariantList TagService::tree(AppError *error) const
{
    const QList<QVariantMap> rows = m_db->select(
        QStringLiteral("SELECT id, parent_id, name FROM tag ORDER BY name COLLATE NOCASE"),
        {}, error);
    if (error != nullptr && !error->ok())
        return {};

    // 按父分组（顶级归入 0 号桶）；SQLite 读出的 parent_id 为 NULL → 顶级
    QHash<qint64, QList<QVariantMap>> children;
    for (const QVariantMap &row : rows) {
        const qint64 parent = row.value(QStringLiteral("parent_id")).isNull()
                                  ? 0
                                  : row.value(QStringLiteral("parent_id")).toLongLong();
        children[parent].append(row);
    }

    // DFS 先序展开：输出项带 depth，供 UI 缩进渲染
    QVariantList out;
    std::function<void(qint64, int)> walk = [&](qint64 parentId, int depth) {
        const QList<QVariantMap> kids = children.value(parentId);
        for (const QVariantMap &kid : kids) {
            const qint64 id = kid.value(QStringLiteral("id")).toLongLong();
            out.append(QVariantMap{{QStringLiteral("id"), id},
                                   {QStringLiteral("parent_id"), parentId},
                                   {QStringLiteral("name"), kid.value(QStringLiteral("name"))},
                                   {QStringLiteral("depth"), depth}});
            walk(id, depth + 1);
        }
    };
    walk(0, 0);
    return out;
}

bool TagService::createTag(qint64 parentId, const QString &name, qint64 *newId, AppError *error)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("标签名不能为空"));
        return false;
    }

    if (parentId > 0) {
        AppError local;
        if (!tagExists(parentId, &local)) {
            if (error != nullptr) {
                *error = local.ok()
                             ? failValidation(QStringLiteral("父标签不存在：#%1").arg(parentId))
                             : local;
            }
            return false;
        }
    } else {
        parentId = 0; // 顶级（落库为 NULL）
    }

    // 同层重名前置检查（部分唯一索引亦会兜底，这里给用户更清晰的提示）
    AppError local;
    QVariant dupCount;
    if (parentId > 0) {
        dupCount = m_db->scalar(
            QStringLiteral("SELECT count(*) FROM tag WHERE name = :name AND parent_id = :parent_id"),
            {{QStringLiteral("name"), trimmed}, {QStringLiteral("parent_id"), parentId}}, &local);
    } else {
        dupCount = m_db->scalar(
            QStringLiteral("SELECT count(*) FROM tag WHERE name = :name AND parent_id IS NULL"),
            {{QStringLiteral("name"), trimmed}}, &local);
    }
    if (failed(local, error))
        return false;
    if (dupCount.toInt() > 0) {
        if (error != nullptr)
            *error = failConflict(QStringLiteral("同级已有同名标签：%1").arg(trimmed));
        return false;
    }

    // 顶级 parent_id 传 NULL（QVariant 无效值 → SQLite NULL）
    const QVariantMap params = {
        {QStringLiteral("name"), trimmed},
        {QStringLiteral("parent_id"), parentId > 0 ? QVariant(parentId) : QVariant()},
    };
    return m_db->execute(
        QStringLiteral("INSERT INTO tag (name, parent_id) VALUES (:name, :parent_id)"), params, newId,
        error);
}

bool TagService::renameTag(qint64 tagId, const QString &name, AppError *error)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("标签名不能为空"));
        return false;
    }
    if (!tagExists(tagId, error))
        return false;

    // 同层重名检查：排除自身；父级可能为 NULL（顶级），故分两种 SQL
    AppError local;
    QVariantMap row;
    const bool found = m_db->selectOne(
        QStringLiteral("SELECT parent_id FROM tag WHERE id = :id"),
        {{QStringLiteral("id"), tagId}}, row, &local);
    if (failed(local, error))
        return false;
    if (!found) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("标签不存在：#%1").arg(tagId));
        return false;
    }

    const qint64 parentId = row.value(QStringLiteral("parent_id")).isNull()
                                ? 0
                                : row.value(QStringLiteral("parent_id")).toLongLong();
    QVariant dupCount;
    if (parentId > 0) {
        dupCount = m_db->scalar(
            QStringLiteral("SELECT count(*) FROM tag WHERE name = :name AND parent_id = :parent_id "
                           "AND id != :self"),
            {{QStringLiteral("name"), trimmed},
             {QStringLiteral("parent_id"), parentId},
             {QStringLiteral("self"), tagId}},
            &local);
    } else {
        dupCount = m_db->scalar(
            QStringLiteral("SELECT count(*) FROM tag WHERE name = :name AND parent_id IS NULL "
                           "AND id != :self"),
            {{QStringLiteral("name"), trimmed}, {QStringLiteral("self"), tagId}}, &local);
    }
    if (failed(local, error))
        return false;
    if (dupCount.toInt() > 0) {
        if (error != nullptr)
            *error = failConflict(QStringLiteral("同级已有同名标签：%1").arg(trimmed));
        return false;
    }

    return m_db->execute(QStringLiteral("UPDATE tag SET name = :name WHERE id = :id"),
                         {{QStringLiteral("name"), trimmed}, {QStringLiteral("id"), tagId}}, error);
}

bool TagService::deleteTag(qint64 tagId, AppError *error)
{
    if (!tagExists(tagId, error))
        return false;
    // 子孙与关联行由外键 ON DELETE CASCADE 清理
    return m_db->execute(QStringLiteral("DELETE FROM tag WHERE id = :id"),
                         {{QStringLiteral("id"), tagId}}, error);
}

QList<qint64> TagService::tagsOfFile(qint64 fileId, AppError *error) const
{
    const QList<QVariantMap> rows = m_db->select(
        QStringLiteral("SELECT tag_id FROM file_tag WHERE file_id = :id ORDER BY tag_id"),
        {{QStringLiteral("id"), fileId}}, error);
    if (error != nullptr && !error->ok())
        return {};

    QList<qint64> ids;
    for (const QVariantMap &row : rows)
        ids.append(row.value(QStringLiteral("tag_id")).toLongLong());
    return ids;
}

QList<qint64> TagService::ancestorIds(qint64 tagId, AppError *error) const
{
    QList<qint64> out;
    qint64 current = tagId;
    int guard = 0; // 防御异常数据造成的环（正常树不会触发）
    while (current > 0 && guard++ < 1024) {
        AppError local;
        const QVariant parent = m_db->scalar(
            QStringLiteral("SELECT parent_id FROM tag WHERE id = :id"),
            {{QStringLiteral("id"), current}}, &local);
        if (failed(local, error))
            return {};
        if (parent.isNull())
            break; // 无处可上（不存在或已是顶级）
        const qint64 parentId = parent.toLongLong();
        if (parentId <= 0)
            break;
        out.append(parentId);
        current = parentId;
    }
    return out;
}

bool TagService::setFileTags(qint64 fileId, const QList<qint64> &tagIds, AppError *error)
{
    AppError local;

    // 1) 数据包存在性（关联表外键也会拦，这里给更明确的提示）
    const QVariant fileCount = m_db->scalar(
        QStringLiteral("SELECT count(*) FROM file WHERE id = :id"),
        {{QStringLiteral("id"), fileId}}, &local);
    if (failed(local, error))
        return false;
    if (fileCount.toInt() != 1) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("数据包不存在：#%1").arg(fileId));
        return false;
    }

    // 2) 去重 + 校验各标签存在（顺带收集名称供报错文案使用）
    QSet<qint64> unique;
    for (qint64 id : tagIds) {
        if (id > 0)
            unique.insert(id);
    }
    QList<qint64> ids = unique.values();
    std::sort(ids.begin(), ids.end());

    QHash<qint64, QString> names;
    for (qint64 id : ids) {
        const QString label = nameOf(id, &local);
        if (failed(local, error))
            return false;
        if (label.isEmpty()) {
            if (error != nullptr)
                *error = failValidation(QStringLiteral("标签不存在：#%1").arg(id));
            return false;
        }
        names.insert(id, label);
    }

    // 3) 反链校验：任一被选标签不得是另一个被选标签的祖先
    for (qint64 id : ids) {
        const QList<qint64> ancestors = ancestorIds(id, &local);
        if (failed(local, error))
            return false;
        for (qint64 ancestor : ancestors) {
            if (unique.contains(ancestor)) {
                if (error != nullptr)
                    *error = failConflict(
                        QStringLiteral("标签「%1」与其祖先「%2」不可同时选择")
                            .arg(names.value(id), names.value(ancestor)));
                return false;
            }
        }
    }

    // 4) 事务内整体替换（先删后插）
    if (!m_db->transaction(error))
        return false;
    if (!m_db->execute(QStringLiteral("DELETE FROM file_tag WHERE file_id = :file_id"),
                       {{QStringLiteral("file_id"), fileId}}, &local)) {
        m_db->rollback();
        if (error != nullptr)
            *error = local;
        return false;
    }
    for (qint64 id : ids) {
        if (!m_db->execute(
                QStringLiteral("INSERT INTO file_tag (file_id, tag_id) VALUES (:file_id, :tag_id)"),
                {{QStringLiteral("file_id"), fileId}, {QStringLiteral("tag_id"), id}}, &local)) {
            m_db->rollback();
            if (error != nullptr)
                *error = local;
            return false;
        }
    }
    if (!m_db->commit(&local)) {
        m_db->rollback();
        if (error != nullptr)
            *error = local;
        return false;
    }
    return true;
}

QList<qint64> TagService::subtreeIds(qint64 tagId, AppError *error) const
{
    // 递归 CTE：自身 + 全部子孙（UNION 天然去重，防异常环）
    const QList<QVariantMap> rows = m_db->select(
        QStringLiteral("WITH RECURSIVE sub(id) AS ("
                       "SELECT id FROM tag WHERE id = :id "
                       "UNION SELECT t.id FROM tag t JOIN sub ON t.parent_id = sub.id) "
                       "SELECT id FROM sub ORDER BY id"),
        {{QStringLiteral("id"), tagId}}, error);
    if (error != nullptr && !error->ok())
        return {};

    QList<qint64> ids;
    for (const QVariantMap &row : rows)
        ids.append(row.value(QStringLiteral("id")).toLongLong());
    return ids;
}

QString TagService::nameOf(qint64 tagId, AppError *error) const
{
    if (tagId <= 0)
        return QString();
    return m_db->scalar(QStringLiteral("SELECT name FROM tag WHERE id = :id"),
                        {{QStringLiteral("id"), tagId}}, error).toString();
}

bool TagService::exportToJson(const QString &dir, QString *outPath, AppError *error) const
{
    const QString target = dir.trimmed();
    if (target.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("未设置默认导出位置"));
        return false;
    }
    if (!QDir().mkpath(target)) {
        if (error != nullptr)
            *error = AppError::fail(
                AppError::Io,
                QStringLiteral("导出目录无法创建：%1").arg(QDir::toNativeSeparators(target)));
        return false;
    }

    // 一次性读全量标签并按父分组（排序与 tree() 一致，导出顺序即 UI 顺序）
    AppError local;
    const QList<QVariantMap> rows = m_db->select(
        QStringLiteral("SELECT id, parent_id, name FROM tag ORDER BY name COLLATE NOCASE"),
        {}, &local);
    if (failed(local, error))
        return false;

    QHash<qint64, QList<QVariantMap>> children;
    for (const QVariantMap &row : rows) {
        const qint64 parent = row.value(QStringLiteral("parent_id")).isNull()
                                  ? 0
                                  : row.value(QStringLiteral("parent_id")).toLongLong();
        children[parent].append(row);
    }

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 1);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("tags"), buildNodes(children, 0));

    // 文件名带时间戳，不覆盖历史导出
    const QString filePath = QDir(target).filePath(
        QStringLiteral("tags_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".json"));

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = AppError::fail(
                AppError::Io,
                QStringLiteral("无法写入标签文件：%1").arg(QDir::toNativeSeparators(filePath)));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr)
            *error = AppError::fail(
                AppError::Io,
                QStringLiteral("标签文件提交失败：%1").arg(QDir::toNativeSeparators(filePath)));
        return false;
    }

    if (outPath != nullptr)
        *outPath = filePath;
    return true;
}

bool TagService::importFromJson(const QString &path, int *created, int *merged, AppError *error)
{
    if (created != nullptr)
        *created = 0;
    if (merged != nullptr)
        *merged = 0;

    QFile file(path);
    if (!file.exists()) {
        if (error != nullptr)
            *error = failValidation(
                QStringLiteral("标签文件不存在：%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = AppError::fail(
                AppError::Io,
                QStringLiteral("无法读取标签文件：%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error != nullptr)
            *error = failValidation(
                QStringLiteral("标签文件不是合法 JSON：%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QJsonValue tagsValue = doc.object().value(QStringLiteral("tags"));
    if (!tagsValue.isArray()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("标签文件缺少 tags 数组"));
        return false;
    }
    const QJsonArray tags = tagsValue.toArray();

    // 整体结构校验先行：任何非法节点都拒绝，保证"要么全导入、要么全不动"
    if (!validateTagNodes(tags, error))
        return false;

    // 预载「父 id + 名称 → 标签 id」索引：导入跑在写事务里，另一读连接看不到未提交的新行，
    // 故同层去重靠这份内存索引（新建节点即时登记）而非事务内查询
    AppError local;
    QHash<QString, qint64> existing;
    const QList<QVariantMap> rows = m_db->select(
        QStringLiteral("SELECT id, parent_id, name FROM tag"), {}, &local);
    if (failed(local, error))
        return false;
    for (const QVariantMap &row : rows) {
        const qint64 parent = row.value(QStringLiteral("parent_id")).isNull()
                                  ? 0
                                  : row.value(QStringLiteral("parent_id")).toLongLong();
        existing.insert(taggedKey(parent, row.value(QStringLiteral("name")).toString()),
                        row.value(QStringLiteral("id")).toLongLong());
    }

    if (!m_db->transaction(&local)) {
        if (error != nullptr)
            *error = local;
        return false;
    }
    int createdCount = 0;
    int mergedCount = 0;
    for (const QJsonValue &value : tags) {
        if (!importNode(value.toObject().toVariantMap(), 0, &createdCount, &mergedCount, existing,
                        &local)) {
            m_db->rollback();
            if (error != nullptr)
                *error = local;
            return false;
        }
    }
    if (!m_db->commit(&local)) {
        m_db->rollback();
        if (error != nullptr)
            *error = local;
        return false;
    }

    if (created != nullptr)
        *created = createdCount;
    if (merged != nullptr)
        *merged = mergedCount;
    return true;
}

bool TagService::importNode(const QVariantMap &node, qint64 parentId, int *created, int *merged,
                            QHash<QString, qint64> &existing, AppError *error)
{
    const QString name = node.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("标签文件格式非法：存在名称为空的标签"));
        return false;
    }

    // 同层同名 → 复用已有标签（幂等）；否则新建并登记进索引
    const QString key = taggedKey(parentId, name);
    qint64 nodeId = existing.value(key, 0);
    if (nodeId > 0) {
        ++(*merged);
    } else {
        AppError local;
        const QVariantMap params = {
            {QStringLiteral("name"), name},
            {QStringLiteral("parent_id"), parentId > 0 ? QVariant(parentId) : QVariant()},
        };
        if (!m_db->execute(QStringLiteral("INSERT INTO tag (name, parent_id) "
                                          "VALUES (:name, :parent_id)"),
                           params, &nodeId, &local)) {
            if (error != nullptr)
                *error = local;
            return false;
        }
        existing.insert(key, nodeId);
        ++(*created);
    }

    const QVariantList children = node.value(QStringLiteral("children")).toList();
    for (const QVariant &child : children) {
        if (!importNode(child.toMap(), nodeId, created, merged, existing, error))
            return false;
    }
    return true;
}
