#pragma once

#include <QString>
#include <QVariantList>

#include "common/AppError.h"

class AppPaths;
class EventBus;
class LibraryService;
class StorageService;

// file 总表服务（开发文档 3.4 / 6.2 / 6.4 / 6.9 / 6.10）：
// 包查询与标题修改、评分算法、浏览记忆读写、包删除（含实体清理）。
// 纪律：
// - 评分入口唯一（类型包不得直写 score）；0 = 未评分；
// - 浏览记忆写回不触碰 file.updated_time；
// - 删除顺序：事务删 db 行（分表/关联表靠外键级联）→ 删包文件夹 → 删封面。
class PackageService
{
public:
    PackageService(AppPaths *paths, LibraryService *library, StorageService *storage,
                   EventBus *events);

    // ---- 查询 ----
    // 列出包（form 为空 → 全部类型），按 id 升序
    QVariantList listPackages(const QString &form, AppError *error = nullptr) const;

    // 搜索范围：标题 / 创作者（创作者匹配正式名与别名）
    enum SearchScope
    {
        SearchTitle = 0,
        SearchCreator = 1,
    };

    // 带条件分页查询的条件集合（M4 资源页 → M5 统一筛选弹窗）：
    // form 空=全部类型；keyword 空=不搜索；scope 决定 keyword 匹配标题还是创作者；
    // creationFrom/To：创作日期闭区间（file.creation_date，YYYY-MM-DD，空=不限）；
    // createdFrom/To：入库时间闭区间（UI 传本地日期 YYYY-MM-DD，内部换算为 UTC 时刻比较，空=不限）。
    struct QueryFilters
    {
        QString form;
        QString keyword;
        int scope = SearchTitle;
        QString creationFrom;
        QString creationTo;
        QString createdFrom;
        QString createdTo;
    };

    // 带条件分页查询（M4 资源页）：
    // filters 见 QueryFilters；offset/limit 决定当前页；total 输出总行数（供分页计数，可传空忽略）。
    QVariantList queryPackages(const QueryFilters &filters, int offset, int limit, int *total,
                               AppError *error = nullptr) const;

    // 单包详情：file 行（含核心列 creation_date）+ type_form + 分表自有字段合并
    // + 创作者多对多（creatorName=主要创作者 / creators=全列表 / creatorCount=数量）
    QVariantMap packageDetail(qint64 id, AppError *error = nullptr) const;

    // ---- 修改 ----
    bool updateTitle(qint64 id, const QString &title, AppError *error = nullptr);

    // 创作日期（核心列 file.creation_date，格式固定 YYYY-MM-DD）：空串或非法格式返回 Validation 错误；
    // 成功后广播 PackageUpdated。
    bool updateCreationDate(qint64 id, const QString &date, AppError *error = nullptr);

    // 评分（开发文档 6.9）：首评直接生效；再评 score = round(旧×(1-w) + 新×w)；
    // 0 = 清除评分。触发 PackageUpdated。
    bool ratePackage(qint64 id, int newScore, AppError *error = nullptr);

    double scoreRecentWeight() const { return m_scoreRecentWeight; }
    void setScoreRecentWeight(double weight) { m_scoreRecentWeight = qBound(0.0, weight, 1.0); }

    // ---- 浏览记忆（开发文档 6.4）----
    // position 为 JSON 字符串（语义由类型自定义）；无记录返回空串
    QString browseState(qint64 id, AppError *error = nullptr) const;

    // 写回浏览位置（UPSERT）；不触碰 file.updated_time
    bool writeBrowseState(qint64 id, const QString &positionJson, AppError *error = nullptr);

    // ---- 删除（开发文档 6.10）----
    bool deletePackage(qint64 id, AppError *error = nullptr);

private:
    // 当前库上下文；无打开库时返回 false
    bool requireCurrentLibrary(QString *libraryName, QString *linkAddress,
                               AppError *error) const;

    AppPaths *m_paths;
    LibraryService *m_library;
    StorageService *m_storage;
    EventBus *m_events;
    double m_scoreRecentWeight = 0.6; // 设置项 scoreRecentWeight，M5 接入设置界面
};
