#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "common/AppError.h"

// 单个库在 config.json 中的登记信息（开发文档 4.2）
struct LibraryEntry
{
    QString name;           // 库名（也是 envs 下目录名）
    QString linkAddress;    // 实际库根路径（UNIX 风格存储）
    QStringList support;    // 已启用类型 form 清单
    qint64 creationDate = 0;// 秒级时间戳
    int schemaVersion = 1;  // 核心表版本（以 db 内 PRAGMA user_version 为准，此字段仅作快速检视）
};

// config.json 的内存模型与读写。
// 全局唯一，位于 envs/config.json；每库信息是其中一节，不另设每库文件。
class LibraryConfig
{
public:
    // 文件不存在时按空配置返回 true（首次启动）；解析失败返回 false
    bool load(const QString &path, AppError *error = nullptr);

    // 原子写（QSaveFile：先写临时文件再替换，写一半崩溃不会损坏原文件）
    bool save(const QString &path, AppError *error = nullptr) const;

    QString currentName;                 // currentDb
    QStringList createdDb;               // 建库顺序清单

    bool contains(const QString &name) const;
    LibraryEntry entry(const QString &name) const; // 不存在返回 name 填充的默认值
    void upsert(const LibraryEntry &entry);
    void remove(const QString &name);
    // 重命名：保持建库顺序不变地重键（from→to），并同步 currentName 指向。
    // 调用方需保证 from 存在、to 不存在。
    void rename(const QString &from, const QString &to);
    QList<LibraryEntry> entriesInOrder() const;

private:
    QHash<QString, LibraryEntry> m_entries;
};
