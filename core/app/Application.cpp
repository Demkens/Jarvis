#include "Application.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "import/ImportService.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
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
    m_typeEngine = std::make_unique<TypeEngine>(m_paths.get(), m_library.get(),
                                                m_typePackages.get(), m_creators.get(),
                                                m_packages.get(), m_eventBus.get(), this);
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
