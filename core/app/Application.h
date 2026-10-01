#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QQmlApplicationEngine;
class AppPaths;
class CreatorService;
class EventBus;
class ImportService;
class PackageService;
class LibraryService;
class SettingsService;
class StorageService;
class TypeEngine;
class TypePackageManager;

// 组合根：持有并装配全部核心服务，向 QML 注入上下文。
// 服务之间不直接互相持有，协作经事件总线或显式调用。
class Application : public QObject
{
    Q_OBJECT

public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    // 装配顺序：路径/事件 → 类型包发现 → 库服务启动（libraryOverride 非空时为多进程
    // 入口指定的库；为空则恢复上次库）
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
    std::unique_ptr<ImportService> m_import;
    std::unique_ptr<SettingsService> m_settings;
    std::unique_ptr<TypeEngine> m_typeEngine;
};
