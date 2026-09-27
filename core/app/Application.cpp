#include "Application.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "import/ImportService.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "settings/SettingsService.h"
#include "storage/StorageService.h"
#include "types/TypeEngine.h"
#include "types/TypePackageManager.h"

Application::Application(QObject *parent)
    : QObject(parent)
{
}

Application::~Application() = default;

bool Application::initialize()
{
    m_paths = std::make_unique<AppPaths>(AppPaths::defaultEnvsRoot());
    m_eventBus = std::make_unique<EventBus>(this);

    m_typePackages = std::make_unique<TypePackageManager>(this);
    m_typePackages->discoverAndLoad();

    m_library = std::make_unique<LibraryService>(m_paths.get(), m_typePackages.get(),
                                                 m_eventBus.get(), this);
    m_library->startup();

    // 依赖序：库服务（持有 DbAccess）→ 创作者 → 存储 → 包服务
    m_creators = std::make_unique<CreatorService>(&m_library->dbAccess());
    m_storage = std::make_unique<StorageService>(m_paths.get(), m_library.get(),
                                                 m_creators.get(), m_eventBus.get());
    m_packages = std::make_unique<PackageService>(m_paths.get(), m_library.get(),
                                                  m_storage.get(), m_eventBus.get());
    m_import = std::make_unique<ImportService>(m_library.get(), m_storage.get(),
                                               m_typePackages.get(), this);

    // M5 全局设置：加载失败不阻塞启动（回退默认值，只记警告）；成功后立即应用到各服务
    m_settings = std::make_unique<SettingsService>();
    AppError settingsError;
    if (!m_settings->load(SettingsService::defaultPath(m_paths.get()), &settingsError))
        qWarning().noquote() << "[settings] 加载失败，使用默认值:" << settingsError.message;
    m_storage->setCoverLongEdge(m_settings->coverLongEdge());
    m_packages->setScoreRecentWeight(m_settings->scoreRecentWeight() / 100.0);

    m_typeEngine = std::make_unique<TypeEngine>(m_paths.get(), m_library.get(),
                                                m_typePackages.get(), m_creators.get(),
                                                m_packages.get(), m_storage.get(),
                                                m_eventBus.get(), m_settings.get(), this);
    return true;
}

void Application::bindToQml(QQmlApplicationEngine &engine)
{
    engine.rootContext()->setContextProperty(QStringLiteral("typePackageManager"),
                                             m_typePackages.get());
    engine.rootContext()->setContextProperty(QStringLiteral("libraryService"),
                                             m_library.get());
    engine.rootContext()->setContextProperty(QStringLiteral("typeEngine"), m_typeEngine.get());
    engine.rootContext()->setContextProperty(QStringLiteral("importService"), m_import.get());
}
