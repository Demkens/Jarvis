cmake_minimum_required(VERSION 3.21)

project(Jarvis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 图表用 Qt Graphs（M7 统计页）。其 2D 渲染基于 Qt Quick Shapes，与纯 QML 应用同路，
# 不需要 Widgets / QApplication（旧的 Qt Charts 因 2D 走 Graphics View 才必须带上二者，
# 且已自 Qt 6.10 起废弃），故此处不再请求 Charts 与 Widgets 组件。
# Core5Compat：小说源 txt 的 GB18030 解码依赖 QTextCodec（Qt6 未提供等价内建 API）
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Sql Graphs Core5Compat)

qt_standard_project_setup(REQUIRES 6.5)

# qml 分目录组织（pages/dialogs），不逐目录写 qmldir
qt_policy(SET QTP0004 OLD)

# 图标等图片资源：显式开 AUTORCC，把 images.qrc 作为 jarvis 源由 rcc 编译（按 qrc 内部 prefix/alias）
set(CMAKE_AUTORCC ON)

# ---- jarvis_core：全部核心逻辑（可执行与无头测试共用此静态库）----
set (CORE_HEADERS
    core/app/Application.h
    core/common/Logging.h
    core/common/MediaTypes.h
    core/creator/CreatorService.h
    core/dedup/ContentHasher.h
    core/dedup/DedupBackfillWorker.h
    core/dedup/DedupScanWorker.h
    core/dedup/DedupService.h
    core/dedup/ImageHasher.h
    core/events/EventBus.h
    core/import/ImportService.h
    core/library/AppPaths.h
    core/library/DbAccess.h
    core/library/LibraryConfig.h
    core/library/LibraryService.h
    core/note/NoteService.h
    core/package/PackageService.h
    core/schema/SchemaManager.h
    core/settings/SettingsService.h
    core/stats/StatsService.h
    core/storage/StorageService.h
    core/tag/TagService.h
    core/text/BookSplitter.h
    core/types/TypeEngine.h
    core/types/TypePackage.h
    core/types/TypePackageManager.h
    core/ui/PackageViewContext.h
)

add_library(jarvis_core STATIC
    ${CORE_HEADERS}
    core/app/Application.cpp
    core/common/Logging.cpp
    core/dedup/ContentHasher.cpp
    core/dedup/DedupBackfillWorker.cpp
    core/dedup/DedupScanWorker.cpp
    core/dedup/DedupService.cpp
    core/dedup/ImageHasher.cpp
    core/types/TypePackage.cpp
    core/types/TypePackageManager.cpp
    core/events/EventBus.cpp
    core/library/AppPaths.cpp
    core/library/LibraryConfig.cpp
    core/library/DbAccess.cpp
    core/schema/SchemaManager.cpp
    core/library/LibraryService.cpp
    core/note/NoteService.cpp
    core/creator/CreatorService.cpp
    core/storage/StorageService.cpp
    core/tag/TagService.cpp
    core/package/PackageService.cpp
    core/settings/SettingsService.cpp
    core/stats/StatsService.cpp
    core/import/ImportService.cpp
    core/text/BookSplitter.cpp
    core/types/TypeEngine.cpp
    core/ui/PackageViewContext.cpp
)

target_include_directories(jarvis_core PUBLIC core)

# JARVIS_SOURCE_DIR：开发期类型包目录回退（部署结构以可执行文件旁 plugins/ 为准）
target_compile_definitions(jarvis_core PUBLIC
    JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JARVIS_CORE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(jarvis_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Sql Qt6::Core5Compat
)

# ---- jarvis：主程序（组合根 + QML）----
qt_add_executable(jarvis core/main.cpp)

qt_add_qml_module(jarvis
    URI Jarvis
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/ResourcePage.qml
        qml/pages/StatisticsPage.qml
        qml/pages/NotePage.qml
        qml/windows/DetailWindow.qml
        qml/include/TopBar.qml
        qml/include/FileMenu.qml
        qml/include/WindowControls.qml
        qml/include/WindowButton.qml
        qml/include/NavItem.qml
        qml/include/LeftBar.qml
        qml/include/DateField.qml
        qml/include/TagTree.qml
        qml/include/ToolsMenu.qml
        qml/include/AppDialogs.qml
        qml/include/StatCard.qml
        qml/include/TypePieChart.qml
        qml/include/CreatorBarChart.qml
        qml/include/MonthLineChart.qml
        qml/include/TagBarChart.qml
        qml/include/HeatmapChart.qml
        qml/dialogs/ImportDialog.qml
        qml/dialogs/SettingsDialog.qml
        qml/dialogs/FilterDialog.qml
        qml/dialogs/EditPackageDialog.qml
        qml/dialogs/TagManagerDialog.qml
        qml/dialogs/TagPickDialog.qml
        qml/windows/ViewerWindow.qml
        qml/windows/LibraryManagerWindow.qml
        qml/windows/DedupWindow.qml
)

target_link_libraries(jarvis PRIVATE jarvis_core)

# Qt Graphs：统计页四张图用其 QML API（GraphsView/PieSeries/BarSeries/LineSeries）。
# 纯 QML 渲染路径，不引入 Widgets，故 main 仍用 QGuiApplication。
target_link_libraries(jarvis PRIVATE Qt6::Graphs)

# 图标等通用图片资源：作为源加入 jarvis，由 AUTORCC 编译（按 qrc 内部 prefix/alias）
target_sources(jarvis PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/images.qrc")

# 控制台窗口：Debug 保留（便于开发期直接观察 stderr 回显与日志），其余配置置 TRUE（发布形态无控制台）
set_target_properties(jarvis PROPERTIES WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")

# 构建后把类型包拷贝到产物目录，形成 <exe>/plugins/types/<form>/ 部署结构
add_custom_command(TARGET jarvis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/plugins" "$<TARGET_FILE_DIR:jarvis>/plugins"
)

# ---- 部署：生成可直接拷走的绿色目录 ----
# 用法：cmake --install <构建目录> --prefix <目标目录>（如 --prefix dist）
# windeployqt 由生成的脚本在 install 阶段调用，收拢 Qt 运行库、平台插件与 QML 模块到 exe 同级；
# 类型包随之落于 <目标目录>/plugins/types/<form>/，envs/ 在首次运行时于 exe 旁自建。
install(TARGETS jarvis RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/plugins" DESTINATION plugins)

qt_generate_deploy_app_script(
    TARGET jarvis
    OUTPUT_SCRIPT jarvis_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${jarvis_deploy_script})