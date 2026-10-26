#include "Application.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "creator/CreatorService.h"
#include "dedup/DedupService.h"
#include "events/EventBus.h"
#include "import/ImportService.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "note/NoteService.h"
#include "package/PackageService.h"
#include "settings/SettingsService.h"
#include "stats/StatsService.h"
#include "storage/StorageService.h"
#include "tag/TagService.h"
#include "types/TypeEngine.h"
#include "types/TypePackageManager.h"

Application::Application(QObject *parent)
    : QObject(parent)
{
}

Application::~Application() = default;

bool Application::initialize(const QString &libraryOverride)
{
    m_paths = std::make_unique<AppPaths>(AppPaths::defaultEnvsRoot());
    m_eventBus = std::make_unique<EventBus>(this);

    m_typePackages = std::make_unique<TypePackageManager>(this);
    m_typePackages->discoverAndLoad();

    m_library = std::make_unique<LibraryService>(m_paths.get(), m_typePackages.get(),
                                                 m_eventBus.get(), this);
    m_library->startup(libraryOverride);

    // 依赖序：库服务（持有 DbAccess）→ 创作者 / 标签 → 存储 → 包服务
    m_creators = std::make_unique<CreatorService>(&m_library->dbAccess());
    m_tags = std::make_unique<TagService>(&m_library->dbAccess());
    // M8a 短笔记：依赖当前库（元数据）与 AppPaths（note 目录）
    m_notes = std::make_unique<NoteService>(m_library.get(), m_paths.get(), this);
    m_storage = std::make_unique<StorageService>(m_paths.get(), m_library.get(),
                                                 m_creators.get(), m_eventBus.get());
    m_packages = std::make_unique<PackageService>(m_paths.get(), m_library.get(),
                                                  m_storage.get(), m_eventBus.get());
    // M9 查重：图片内容指纹与完全重复预检；导入服务在成功后调用它写入指纹
    m_dedup = std::make_unique<DedupService>(m_library.get(), this);
    m_import = std::make_unique<ImportService>(m_library.get(), m_storage.get(),
                                               m_typePackages.get(), m_dedup.get(), this);

    // M7 数据统计：只读聚合门面，借库/包/标签三个服务取数，订阅事件置脏（懒重算）
    m_stats = std::make_unique<StatsService>(m_library.get(), m_packages.get(), m_tags.get(),
                                             m_eventBus.get(), this);

    // 补全存量行的封面长宽比（瀑布流布局用）：库打开/切换时各跑一次（无待补行时开销极小）
    m_storage->backfillAspectRatios();
    connect(m_library.get(), &LibraryService::openChanged, this, [this]() {
        m_storage->backfillAspectRatios();
    });

    // M5 全局设置：加载失败不阻塞启动（回退默认值，只记警告）；成功后立即应用到各服务
    m_settings = std::make_unique<SettingsService>();
    AppError settingsError;
    if (!m_settings->load(SettingsService::defaultPath(m_paths.get()), &settingsError))
        qWarning().noquote() << "[settings] 加载失败，使用默认值:" << settingsError.message;
    m_packages->setScoreRecentWeight(m_settings->scoreRecentWeight() / 100.0);

    m_typeEngine = std::make_unique<TypeEngine>(m_paths.get(), m_library.get(),
                                                m_typePackages.get(), m_creators.get(),
                                                m_packages.get(), m_tags.get(),
                                                m_eventBus.get(), m_settings.get(), this);
    return true;
}

void Application::bindToQml(QQmlApplicationEngine &engine)
{
    engine.rootContext()->setContextProperty(QStringLiteral("typePackageManager"), m_typePackages.get());
    engine.rootContext()->setContextProperty(QStringLiteral("libraryService"), m_library.get());
    engine.rootContext()->setContextProperty(QStringLiteral("typeEngine"), m_typeEngine.get());
    engine.rootContext()->setContextProperty(QStringLiteral("importService"), m_import.get());
    engine.rootContext()->setContextProperty(QStringLiteral("dedupService"), m_dedup.get());
    engine.rootContext()->setContextProperty(QStringLiteral("statsService"), m_stats.get());
    engine.rootContext()->setContextProperty(QStringLiteral("noteService"), m_notes.get());
}
