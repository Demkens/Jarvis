#include "CreatorService.h"

#include "library/DbAccess.h"

namespace {

AppError failValidation(const QString &message)
{
    return AppError::fail(AppError::Validation, message);
}

} // namespace

CreatorService::CreatorService(DbAccess *db)
    : m_db(db)
{
}

qint64 CreatorService::resolveOrCreate(const QString &name, int kind, AppError *error)
{
    const QString trimmed = name.trimmed();

    // 空名 → anonymous（id=0，建库时已预置）
    if (trimmed.isEmpty())
        return 0;

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
    if (!m_db->execute(QStringLiteral("INSERT INTO creator (name, kind) VALUES (:name, :kind)"),
                       {{QStringLiteral("name"), trimmed}, {QStringLiteral("kind"), kind}},
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
    // 创作者存在性检查（外键也会拦，这里给出更明确的消息）
    const QVariant exists = m_db->scalar(
        QStringLiteral("SELECT count(*) FROM creator WHERE id = :id"),
        {{QStringLiteral("id"), creatorId}}, error);
    if (exists.toInt() == 0) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("创作者不存在：#%1").arg(creatorId));
        return false;
    }

    // 幂等：同一创作者的重复别名视为成功
    return m_db->execute(QStringLiteral("INSERT OR IGNORE INTO alias (id, alias) VALUES (:id, :alias)"),
                         {{QStringLiteral("id"), creatorId}, {QStringLiteral("alias"), trimmed}},
                         error);
}

bool CreatorService::setKind(qint64 creatorId, int kind, AppError *error)
{
    if (kind < Person || kind > Official) {
        if (error != nullptr)
            *error = failValidation(QStringLiteral("kind 取值须为 1/2/3"));
        return false;
    }
    return m_db->execute(QStringLiteral("UPDATE creator SET kind = :kind WHERE id = :id"),
                         {{QStringLiteral("kind"), kind}, {QStringLiteral("id"), creatorId}},
                         error);
}

QString CreatorService::displayName(qint64 creatorId, AppError *error) const
{
    if (creatorId <= 0)
        return QStringLiteral("anonymous");
    return m_db->scalar(QStringLiteral("SELECT name FROM creator WHERE id = :id"),
                        {{QStringLiteral("id"), creatorId}}, error).toString();
}
