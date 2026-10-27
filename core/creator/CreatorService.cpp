#include "CreatorService.h"

#include <QHash>
#include <QRegularExpression>
#include <QVariantMap>

#include "library/DbAccess.h"

namespace {

AppError failValidation(const QString &message)
{
    return AppError::fail(AppError::Validation, message);
}

// 名称/别名若含 splitNames 使用的分隔符，将永远无法作为单个名字被解析，
// 因此在手工命名入口直接拒绝，避免制造“查不到”的记录。
bool hasNameDelimiter(const QString &name)
{
    return name.contains(QRegularExpression(QStringLiteral("[,，;；、|\\n\\r]")));
}

// LIKE 子串匹配前转义通配符，避免用户输入的 % 或 _ 被当作模式
QString escapeLike(QString text)
{
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    text.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return text;
}

} // namespace

CreatorService::CreatorService(DbAccess *db)
    : m_db(db)
{
}

QList<CreatorSummary> CreatorService::listCreators(const QString &keyword, AppError *error) const
{
    const QString trimmed = keyword.trimmed();
    QVariantMap params;
    QString where;
    if (!trimmed.isEmpty()) {
        params.insert(QStringLiteral("like"),
                      QStringLiteral("%") + escapeLike(trimmed) + QStringLiteral("%"));
        where = QStringLiteral(
            " WHERE (c.name LIKE :like ESCAPE '\\'"
            " OR EXISTS (SELECT 1 FROM alias a2 WHERE a2.id = c.id"
            " AND a2.alias LIKE :like ESCAPE '\\'))");
    }

    const QString sql = QStringLiteral(
                            "SELECT c.id, c.name, "
                            "(SELECT COUNT(*) FROM file_creator fc WHERE fc.creator_id = c.id) "
                            "AS package_count, a.alias "
                            "FROM creator c LEFT JOIN alias a ON a.id = c.id")
                        + where
                        + QStringLiteral(" ORDER BY c.name COLLATE NOCASE, c.id, "
                                         "a.alias COLLATE NOCASE");

    const QList<QVariantMap> rows = m_db->select(sql, params, error);
    if (error != nullptr && !error->ok())
        return {};

    QList<CreatorSummary> out;
    QHash<qint64, int> indexById;
    for (const QVariantMap &row : rows) {
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        int index = indexById.value(id, -1);
        if (index < 0) {
            CreatorSummary summary;
            summary.id = id;
            summary.name = row.value(QStringLiteral("name")).toString();
            summary.packageCount = row.value(QStringLiteral("package_count")).toInt();
            index = out.size();
            indexById.insert(id, index);
            out.append(summary);
        }
        const QString alias = row.value(QStringLiteral("alias")).toString();
        if (!alias.isEmpty() && !out[index].aliases.contains(alias))
            out[index].aliases.append(alias);
    }
    return out;
}

bool CreatorService::renameCreator(qint64 creatorId, const QString &newName, AppError *error)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("正式名不能为空"));
        return false;
    }
    if (trimmed == anonymousName()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("「%1」是系统保留名，不能作为正式名")
                                        .arg(anonymousName()));
        return false;
    }
    if (hasNameDelimiter(trimmed)) {
        if (error != nullptr)
            *error = failValidation(
                QStringLiteral("名称不能包含逗号、分号、顿号、竖线或换行"));
        return false;
    }

    AppError nameError;
    const QString currentName = displayName(creatorId, &nameError);
    if (!nameError.ok()) {
        if (error != nullptr)
            *error = nameError;
        return false;
    }
    if (currentName.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("创作者不存在：#%1").arg(creatorId));
        return false;
    }
    if (currentName == anonymousName()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("默认创作者「%1」不可重命名")
                                        .arg(anonymousName()));
        return false;
    }

    AppError findError;
    const qint64 occupied = findByName(trimmed, &findError);
    if (!findError.ok()) {
        if (error != nullptr)
            *error = findError;
        return false;
    }
    if (occupied >= 0 && occupied != creatorId) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Conflict,
                                    QStringLiteral("正式名「%1」已被其他创作者使用").arg(trimmed));
        return false;
    }

    // 正式名不能与别人的别名同名：resolveOrCreate 先按正式名精确匹配，
    // 否则该别名的持有者将永远无法用这个写法被解析到，形成隐性歧义。
    const QList<qint64> aliasOwners = findByAlias(trimmed, &findError);
    if (!findError.ok()) {
        if (error != nullptr)
            *error = findError;
        return false;
    }
    for (qint64 ownerId : aliasOwners) {
        if (ownerId != creatorId) {
            if (error != nullptr)
                *error = AppError::fail(
                    AppError::Conflict,
                    QStringLiteral("名称「%1」已被其他创作者用作别名").arg(trimmed));
            return false;
        }
    }

    return m_db->execute(QStringLiteral("UPDATE creator SET name = :name WHERE id = :id"),
                         {{QStringLiteral("name"), trimmed}, {QStringLiteral("id"), creatorId}},
                         error);
}

bool CreatorService::removeAlias(qint64 creatorId, const QString &alias, AppError *error)
{
    const QString trimmed = alias.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("别名不能为空"));
        return false;
    }
    if (creatorId <= 0) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("默认创作者不接纳别名"));
        return false;
    }
    // 删除不存在的别名视为成功（幂等），便于界面重试
    return m_db->execute(
        QStringLiteral("DELETE FROM alias WHERE id = :id AND alias = :alias"),
        {{QStringLiteral("id"), creatorId}, {QStringLiteral("alias"), trimmed}}, error);
}

qint64 CreatorService::resolveOrCreate(const QString &name, AppError *error)
{
    const QString trimmed = name.trimmed();

    // 空名 → 默认创作者「佚名」（建库/迁移时保证存在，按名解析即可）
    if (trimmed.isEmpty())
        return resolveOrCreate(anonymousName(), error);

    // 1. 精确名
    AppError nameError;
    const qint64 byName = findByName(trimmed, &nameError);
    if (!nameError.ok()) {
        if (error != nullptr)
            *error = nameError;
        return -1;
    }
    if (byName >= 0)
        return byName;

    // 2. 别名自动匹配正式名；歧义交由用户裁决（开发文档 6.6）
    AppError aliasError;
    const QList<qint64> aliasHits = findByAlias(trimmed, &aliasError);
    if (!aliasError.ok()) {
        if (error != nullptr)
            *error = aliasError;
        return -1;
    }
    if (aliasHits.size() == 1)
        return aliasHits.first();
    if (aliasHits.size() > 1) {
        QStringList candidates;
        for (qint64 id : aliasHits)
            candidates.append(QStringLiteral("#%1").arg(id));
        if (error != nullptr) {
            *error = AppError::fail(
                AppError::Conflict,
                QStringLiteral("别名「%1」对应多个创作者（%2），请改用正式名")
                    .arg(trimmed, candidates.join(QStringLiteral("、"))));
        }
        return -1;
    }

    // 3. find-or-create
    qint64 newId = 0;
    if (!m_db->execute(QStringLiteral("INSERT INTO creator (name) VALUES (:name)"),
                       {{QStringLiteral("name"), trimmed}},
                       &newId, error))
        return -1;
    return newId;
}

qint64 CreatorService::findByName(const QString &name, AppError *error) const
{
    const QVariant value = m_db->scalar(QStringLiteral("SELECT id FROM creator WHERE name = :name"),
                                        {{QStringLiteral("name"), name}}, error);
    bool ok = false;
    const qint64 id = value.toLongLong(&ok);
    return ok ? id : -1;
}

QList<qint64> CreatorService::findByAlias(const QString &alias, AppError *error) const
{
    const QList<QVariantMap> rows = m_db->select(
        QStringLiteral("SELECT id FROM alias WHERE alias = :alias ORDER BY id"),
        {{QStringLiteral("alias"), alias}}, error);

    QList<qint64> ids;
    for (const QVariantMap &row : rows)
        ids.append(row.value(QStringLiteral("id")).toLongLong());
    return ids;
}

bool CreatorService::addAlias(qint64 creatorId, const QString &alias, AppError *error)
{
    const QString trimmed = alias.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("别名不能为空"));
        return false;
    }
    if (creatorId <= 0) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("别名必须挂在真实创作者上"));
        return false;
    }
    if (hasNameDelimiter(trimmed)) {
        if (error != nullptr)
            *error = failValidation(
                QStringLiteral("别名不能包含逗号、分号、顿号、竖线或换行"));
        return false;
    }
    // 创作者存在性检查（外键也会拦，这里给出更明确的消息）
    const QVariant exists = m_db->scalar(
        QStringLiteral("SELECT count(*) FROM creator WHERE id = :id"),
        {{QStringLiteral("id"), creatorId}}, error);
    // 先处理 DB 查询失败（保留原始错误），再判断是否存在，避免 toInt()==0 掩盖真实错误
    if (error != nullptr && !error->ok())
        return false;
    if (exists.toInt() == 0) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("创作者不存在：#%1").arg(creatorId));
        return false;
    }

    // 别名不能与别人的正式名同名：正式名解析优先，否则该别名永远不会命中
    const qint64 nameOwner = findByName(trimmed, error);
    if (error != nullptr && !error->ok())
        return false;
    if (nameOwner >= 0 && nameOwner != creatorId) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Conflict,
                                    QStringLiteral("「%1」已是其他创作者的正式名").arg(trimmed));
        return false;
    }

    // 幂等：同一创作者的重复别名视为成功
    return m_db->execute(QStringLiteral("INSERT OR IGNORE INTO alias (id, alias) VALUES (:id, :alias)"),
                         {{QStringLiteral("id"), creatorId}, {QStringLiteral("alias"), trimmed}},
                         error);
}

QString CreatorService::anonymousName()
{
    return QStringLiteral("佚名");
}

QString CreatorService::displayName(qint64 creatorId, AppError *error) const
{
    if (creatorId <= 0)
        return anonymousName();
    return m_db->scalar(QStringLiteral("SELECT name FROM creator WHERE id = :id"),
                        {{QStringLiteral("id"), creatorId}}, error).toString();
}

QStringList CreatorService::splitNames(const QString &text)
{
    // 常见分隔符统一视为一个字符类；QString::split 遇相邻分隔符会产生空项，逐项清理
    const QStringList raw = text.split(QRegularExpression(QStringLiteral("[,，;；、|\\n\\r]")),
                                       Qt::SkipEmptyParts);
    QStringList names;
    names.reserve(raw.size());
    for (const QString &item : raw) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty())
            names.append(trimmed);
    }
    return names;
}
