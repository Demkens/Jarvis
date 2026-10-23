// 组合根, 全程序唯一创建并组装所有服务的地方: 
// 1. 通过initialize函数创建并装配全部核心服务
// 2. 通过bindToQml函数向 QML 注入各服务的上下文（类型包/库/类型引擎/导入/统计）

#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QQmlApplicationEngine;
class AppPaths;
class CreatorService;
class DedupService;
class EventBus;
class ImportService;
class PackageService;
class LibraryService;
class SettingsService;
class StatsService;
class StorageService;
class TagService;
class TypeEngine;
class TypePackageManager;

class Application : public QObject
{
    Q_OBJECT

public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    // 装配顺序：路径/事件 → 类型包发现 → 库服务启动（libraryOverride 非空时为多进程入口指定的库；为空则恢复上次库）
    bool initialize(const QString &libraryOverride = {});

    void bindToQml(QQmlApplicationEngine &engine);

private:
    std::unique_ptr<AppPaths> m_paths;
    std::unique_ptr<EventBus> m_eventBus;
    std::unique_ptr<TypePackageManager> m_typePackages;
    std::unique_ptr<LibraryService> m_library;
    std::unique_ptr<CreatorService> m_creators;
    std::unique_ptr<StorageService> m_storage;
    std::unique_ptr<PackageService> m_packages;
    std::unique_ptr<DedupService> m_dedup;
    std::unique_ptr<ImportService> m_import;
    std::unique_ptr<TagService> m_tags;
    std::unique_ptr<SettingsService> m_settings;
    std::unique_ptr<StatsService> m_stats;
    std::unique_ptr<TypeEngine> m_typeEngine;
};