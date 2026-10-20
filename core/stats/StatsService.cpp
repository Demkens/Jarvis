#include "stats/StatsService.h"

#include <QDebug>

#include "events/EventBus.h"
#include "library/DbAccess.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "tag/TagService.h"

namespace {

// 按创作者柱状图保留的条目上限，其余合并为一项「其他」，避免长尾压扁图形
constexpr int kCreatorTopN = 10;

// 空白快照：库未打开时的统一形状（各数组为空，计数为 0）
QVariantMap emptySnapshot()
{
    return {
        {QStringLiteral("total"), 0},
        {QStringLiteral("typeCount"), 0},
        {QStringLiteral("creatorCount"), 0},
        {QStringLiteral("byType"), QVariantList{}},
        {QStringLiteral("byCreator"), QVariantList{}},
        {QStringLiteral("byMonth"), QVariantList{}},
    };
}

} // namespace

StatsService::StatsService(LibraryService *library, PackageService *packages, TagService *tags,
                          EventBus *events, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_packages(packages)
    , m_tags(tags)
    , m_events(events)
{
    // 库切换：快照属于旧库，置脏待下次访问重算
    QObject::connect(m_events, &EventBus::librarySwitched, this, [this](const QString &) {
        markStale();
    });
    // 包增删改：仅置脏，不在事件内做聚合（批量导入时事件密集）
    QObject::connect(m_events, &EventBus::packageCreated, this,
                     [this](qint64, const QString &) { markStale(); });
    QObject::connect(m_events, &EventBus::packageUpdated, this,
                     [this](qint64) { markStale(); });
    QObject::connect(m_events, &EventBus::packageDeleted, this,
                     [this](qint64) { markStale(); });
}

void StatsService::markStale()
{
    if (m_stale)
        return; // 已脏，无需重复广播
    m_stale = true;
    emit staleChanged();
}

QVariantMap StatsService::snapshot()
{
    if (!m_stale) {
        QVariantMap cached = m_snapshotCache;
        cached.insert(QStringLiteral("ok"), true);
        return cached;
    }
    return recompute();
}

QVariantMap StatsService::refresh()
{
    return recompute();
}

QVariantMap StatsService::recompute()
{
    AppError error;
    const QVariantMap fresh = buildSnapshot(&error);
    if (!error.ok()) {
        qWarning().noquote() << "[stats] 统计快照失败:" << error.message;
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    }

    m_snapshotCache = fresh;
    if (m_stale) {
        m_stale = false;
        emit staleChanged();
    }

    QVariantMap result = m_snapshotCache;
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QVariantMap StatsService::buildSnapshot(AppError *error) const
{
    // 库未打开：返回全零形状（QML 据 libraryService.currentName 区分"无库/空库"空态）
    if (!m_library->isOpen())
        return emptySnapshot();

    DbAccess &db = m_library->dbAccess();
    auto fail = [error](const AppError &local) {
        if (error != nullptr)
            *error = local;
        return QVariantMap{};
    };

    QVariantMap out = emptySnapshot();

    // ① 三个规模数字
    AppError local;
    out.insert(QStringLiteral("total"),
               db.scalar(QStringLiteral("SELECT COUNT(*) FROM file"), {}, &local).toInt());
    if (!local.ok())
        return fail(local);
    out.insert(QStringLiteral("typeCount"),
               db.scalar(QStringLiteral("SELECT COUNT(*) FROM type"), {}, &local).toInt());
    if (!local.ok())
        return fail(local);
    out.insert(QStringLiteral("creatorCount"),
               db.scalar(QStringLiteral("SELECT COUNT(*) FROM creator"), {}, &local).toInt());
    if (!local.ok())
        return fail(local);

    // ② 按类型（饼图）：图例直接用 type.name（中文显示名）；无包的类型不产生扇区
    const QList<QVariantMap> typeRows = db.select(
        QStringLiteral("SELECT t.name AS name, COUNT(f.id) AS cnt FROM type t "
                       "JOIN file f ON f.type_id = t.id GROUP BY t.id "
                       "ORDER BY cnt DESC, t.name ASC"),
        {}, &local);
    if (!local.ok())
        return fail(local);
    QVariantList byType;
    for (const QVariantMap &row : typeRows) {
        byType.append(QVariantMap{{QStringLiteral("name"), row.value(QStringLiteral("name"))},
                                  {QStringLiteral("count"), row.value(QStringLiteral("cnt"))}});
    }
    out.insert(QStringLiteral("byType"), byType);

    // ③ 按创作者（条形图）：每个关联创作者各计 1，合计可大于数据包总数
    //    （联合创作/多创作者），故各条之和不必等于 total。
    const QList<QVariantMap> creatorRows = db.select(
        QStringLiteral("SELECT c.name AS name, COUNT(fc.file_id) AS cnt FROM creator c "
                       "JOIN file_creator fc ON fc.creator_id = c.id GROUP BY c.id "
                       "ORDER BY cnt DESC, c.name ASC"),
        {}, &local);
    if (!local.ok())
        return fail(local);
    QVariantList byCreator;
    int othersCount = 0;
    int othersTotal = 0;
    for (int i = 0; i < creatorRows.size(); ++i) {
        const int count = creatorRows.at(i).value(QStringLiteral("cnt")).toInt();
        if (i < kCreatorTopN) {
            byCreator.append(QVariantMap{{QStringLiteral("name"),
                                          creatorRows.at(i).value(QStringLiteral("name"))},
                                         {QStringLiteral("count"), count},
                                         {QStringLiteral("others"), false}});
        } else {
            othersCount += 1;
            othersTotal += count;
        }
    }
    if (othersCount > 0) {
        byCreator.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("其他")},
                                     {QStringLiteral("count"), othersTotal},
                                     {QStringLiteral("others"), true}});
    }
    out.insert(QStringLiteral("byCreator"), byCreator);

    // ④ 按创作日期（折线图）：creation_date 恒为 YYYY-MM-DD（导入/编辑双向强制），
    //    故取前 7 位即月份，且各月之和等于 total。
    const QList<QVariantMap> monthRows = db.select(
        QStringLiteral("SELECT substr(f.creation_date, 1, 7) AS ym, COUNT(*) AS cnt "
                       "FROM file f GROUP BY ym ORDER BY ym ASC"),
        {}, &local);
    if (!local.ok())
        return fail(local);
    QVariantList byMonth;
    for (const QVariantMap &row : monthRows) {
        byMonth.append(QVariantMap{{QStringLiteral("month"), row.value(QStringLiteral("ym"))},
                                   {QStringLiteral("count"), row.value(QStringLiteral("cnt"))}});
    }
    out.insert(QStringLiteral("byMonth"), byMonth);

    return out;
}

QVariantMap StatsService::tagStats(const QVariantList &tagIds)
{
    if (!m_library->isOpen())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("没有打开的库")}};

    AppError local;
    const int total = m_library->dbAccess()
                          .scalar(QStringLiteral("SELECT COUNT(*) FROM file"), {}, &local)
                          .toInt();
    if (!local.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), local.message}};

    // 解析选中标签 id（去重、忽略非正整数，保持调用顺序即展示顺序）
    QList<qint64> ids;
    for (const QVariant &value : tagIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id > 0 && !ids.contains(id))
            ids.append(id);
    }

    QVariantList items;
    for (qint64 id : ids) {
        const QString name = m_tags->nameOf(id);
        if (name.isEmpty())
            continue; // 标签已被删除：跳过该柱，不中断整次计量

        // 复用资源页筛选的标签语义（单标签 OR = 命中该标签或其全部子孙）；
        // offset/limit 传 0——只要 total 这个计数，不需取回任何行。
        PackageService::QueryFilters filters;
        filters.tagIds.append(id);
        filters.tagMatchAll = false;
        int count = 0;
        AppError queryError;
        m_packages->queryPackages(filters, 0, 0, &count, &queryError);
        if (!queryError.ok())
            return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), queryError.message}};

        items.append(QVariantMap{{QStringLiteral("tagId"), id},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("count"), count},
                                 {QStringLiteral("percent"),
                                  total > 0 ? static_cast<double>(count) * 100.0 / total : 0.0}});
    }

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("total"), total},
            {QStringLiteral("items"), items}};
}