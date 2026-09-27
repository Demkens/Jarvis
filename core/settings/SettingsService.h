#pragma once

#include <QString>
#include <QtGlobal>

#include "common/AppError.h"

class AppPaths;

// 全局设置服务（M5）：把原先硬编码的三个配置项落库为 envs/settings.json。
// 与 LibraryConfig 同款持久化模式（QJsonObject + QSaveFile 原子写），全局生效、不随库。
// 项 / 默认 / 范围：
// - pageSize           每页数量，默认 50，范围 10–200
// - coverLongEdge      封面长边(px)，默认 400，范围 64–2048
// - scoreRecentWeight  评分权重(0–100%)，默认 60，范围 0–100（存整数百分比，应用侧转 double）
// 纪律：
// - 文件缺失 → 全默认值（首次启动）；解析失败 → 置 error 返回 false（调用方回退默认值，不阻塞启动）；
// - setter 内部 clamp 到范围，不依赖调用方先校验，JSON 里越界值也被收拢；
// - 本类只负责读写与范围约束，不持有其他服务；新值如何生效由调用方（Application/TypeEngine）负责。
class SettingsService
{
public:
    // envs 根下 settings.json（与 config.json 同级、不随库）
    static QString defaultPath(const AppPaths *paths);

    // 输入 path：读取并覆盖内存值；缺文件按默认值返回 true，解析失败返回 false 并置 error
    bool load(const QString &path, AppError *error = nullptr);

    // 输入 path：把当前内存值原子写盘（QSaveFile）；失败返回 false 并置 error
    bool save(const QString &path, AppError *error = nullptr) const;

    int pageSize() const { return m_pageSize; }
    void setPageSize(int v) { m_pageSize = qBound(10, v, 200); }

    int coverLongEdge() const { return m_coverLongEdge; }
    void setCoverLongEdge(int v) { m_coverLongEdge = qBound(64, v, 2048); }

    int scoreRecentWeight() const { return m_scoreRecentWeight; }
    void setScoreRecentWeight(int v) { m_scoreRecentWeight = qBound(0, v, 100); }

private:
    int m_pageSize = 50;             // 每页数量
    int m_coverLongEdge = 400;       // 封面长边(px)
    int m_scoreRecentWeight = 60;    // 评分权重(%)，0–100
};
