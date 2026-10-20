// 数据统计服务（M7）：库级只读聚合的 QML 门面。
// 职责：
// - 汇总当前库的规模指标（数据包总数 / 已登记类型数 / 已登记创作者数）；
// - 汇总三个维度的分布（按类型、按创作者 Top10+其他、按创作月份）；
// - 按选中标签做「标签计量」（命中该标签或其全部子孙的包数，语义与资源页筛选一致）。
// 纪律：
// - 只读：不写库、不发事件、不持有连接（借 LibraryService 的 DbAccess）；
// - 基线快照带缓存与脏标记：包事件只置脏，页面回前台调 snapshot() 时才重算，
//   避免批量导入期间反复全表聚合造成卡顿；
// - 标签计量不入缓存（选中的标签随时变化），每次调用即时计算。

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "common/AppError.h"

class EventBus;
class LibraryService;
class PackageService;
class TagService;

class StatsService : public QObject
{
    Q_OBJECT
    // 基线快照是否有待重算的数据变更（QML 可据此提示"数据已过期"）
    Q_PROPERTY(bool stale READ stale NOTIFY staleChanged)

public:
    // 构造：依赖库服务（取连接与当前库）、包服务（标签计量复用其查询）、
    //       标签服务（标签名）、事件总线（订阅变更置脏）。各指针不可为空。
    StatsService(LibraryService *library, PackageService *packages, TagService *tags,
                 EventBus *events, QObject *parent = nullptr);

    bool stale() const { return m_stale; }

    // 读基线快照（总数/类型数/创作者数 + 按类型/按创作者/按创作月份分布）。
    // 输入: 无；输出: {ok, message?, total, typeCount, creatorCount,
    //                 byType:[{name,count}],
    //                 byCreator:[{name,count,others?}],
    //                 byMonth:[{month,count}]}。
    // 缓存已脏（有事件置位）时重算，否则直接返回缓存。
    // 库未打开时返回全零快照（ok=true，各项为空），由 QML 自行展示空态。
    Q_INVOKABLE QVariantMap snapshot();

    // 强制重算基线快照（统计页「刷新」按钮用；语义与 snapshot() 相同，仅忽略脏标记）。
    // 输入: 无；输出: 同 snapshot()。
    Q_INVOKABLE QVariantMap refresh();

    // 标签计量：每个选中标签命中「该标签或其全部子孙」的数据包数与占比。
    // 输入: tagIds —— 标签 id 数组（QVariantList，元素为整数；非正整数忽略）；
    // 输出: {ok, message?, total, items:[{tagId,name,count,percent}]}。
    // 说明: 多标签各计一次，故 count 之和可超 total、percent 之和可超 100%；不存在的标签跳过。
    Q_INVOKABLE QVariantMap tagStats(const QVariantList &tagIds);

signals:
    void staleChanged();

private:
    // 标记基线过期（由事件触发；不立即重算）
    void markStale();
    // 重算基线快照并为成功结果建立缓存（同时清除脏标记）
    QVariantMap recompute();
    // 执行六条只读聚合 SQL 并组装快照（不做缓存判断）
    QVariantMap buildSnapshot(AppError *error) const;

    LibraryService *m_library;
    PackageService *m_packages;
    TagService *m_tags;
    EventBus *m_events;

    bool m_stale = true;         // 初始为脏：首次访问必然重算
    QVariantMap m_snapshotCache; // 最近一次基线快照
};