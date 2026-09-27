#pragma once

#include <QString>

#include "common/AppError.h"

// 运行期路径服务：统一回答"envs 在哪、某库的 db/封面在哪"。
// 目录约定：
//   <envsRoot>/config.json
//   <envsRoot>/<库名>/<库名>.db
//   <envsRoot>/<库名>/cover/
class AppPaths
{
public:
    explicit AppPaths(QString envsRoot);

    // 默认根：可执行文件旁 envs/（部署结构；开发期即 build/envs）
    static QString defaultEnvsRoot();

    // 存储路径统一为 UNIX 风格 '/'（开发文档 5.6）
    static QString normalizeStored(const QString &path);

    // 确保 envs 根目录存在（不存在则创建）
    bool ensureRoot(AppError *error = nullptr) const;

    const QString &envsRoot() const { return m_envsRoot; }

    QString configPath() const;
    QString libraryDir(const QString &name) const;
    QString databasePath(const QString &name) const;
    QString coverDir(const QString &name) const;

private:
    QString m_envsRoot;
};
