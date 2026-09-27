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

    // 单包详情：file 行 + type_form + 分表字段合并（如 draw 的 creator_id/creation_date）
    // + creatorName（creator_id>0 时解析）
    QVariantMap packageDetail(qint64 id, AppError *error = nullptr) const;

    // ---- 修改 ----
    bool updateTitle(qint64 id, const QString &title, AppError *error = nullptr);

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
