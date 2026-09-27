#include "settings/SettingsService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "library/AppPaths.h"

QString SettingsService::defaultPath(const AppPaths *paths)
{
    return paths->envsRoot() + QStringLiteral("/settings.json");
}

bool SettingsService::load(const QString &path, AppError *error)
{
    // 先复位为默认值：重载（重建实例/测试）不会残留上次的修改
    m_pageSize = 50;
    m_coverLongEdge = 400;
    m_scoreRecentWeight = 60;

    QFile file(path);
    if (!file.exists())
        return true; // 首次启动：全默认值

    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("无法读取设置文件：%1").arg(path));
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("设置文件不是合法 JSON：%1").arg(path));
        return false;
    }
    const QJsonObject root = doc.object();

    // setter 内部 clamp：JSON 中出现越界值也被收拢到范围
    setPageSize(root.value(QStringLiteral("pageSize")).toInt(m_pageSize));
    setCoverLongEdge(root.value(QStringLiteral("coverLongEdge")).toInt(m_coverLongEdge));
    setScoreRecentWeight(root.value(QStringLiteral("scoreRecentWeight")).toInt(m_scoreRecentWeight));
    return true;
}

bool SettingsService::save(const QString &path, AppError *error) const
{
    // 父目录不存在（如从未建库时直接保存设置）先创建，保证任意状态下可保存
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    root.insert(QStringLiteral("pageSize"), m_pageSize);
    root.insert(QStringLiteral("coverLongEdge"), m_coverLongEdge);
    root.insert(QStringLiteral("scoreRecentWeight"), m_scoreRecentWeight);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("无法写入设置文件：%1").arg(path));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Config,
                                    QStringLiteral("设置文件提交失败：%1").arg(path));
        return false;
    }
    return true;
}
