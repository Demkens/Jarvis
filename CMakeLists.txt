cmake_minimum_required(VERSION 3.21)

project(Jarvis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Sql)

qt_standard_project_setup(REQUIRES 6.5)

# qml 分目录组织（pages/dialogs），不逐目录写 qmldir
qt_policy(SET QTP0004 OLD)

# ---- jarvis_core：全部核心逻辑（可执行与无头测试共用此静态库）----
set (CORE_HEADERS
    core/app/Application.h
    core/creator/CreatorService.h
    core/events/EventBus.h
    core/import/ImportService.h
    core/library/AppPaths.h
    core/library/DbAccess.h
    core/library/LibraryConfig.h
    core/library/LibraryService.h
    core/package/PackageService.h
    core/schema/SchemaManager.h
    core/storage/StorageService.h
    core/types/TypeEngine.h
    core/types/TypePackage.h
    core/types/TypePackageManager.h
    core/ui/PackageViewContext.h
)

add_library(jarvis_core STATIC
    ${CORE_HEADERS}
    core/app/Application.cpp
    core/types/TypePackage.cpp
    core/types/TypePackageManager.cpp
    core/events/EventBus.cpp
    core/library/AppPaths.cpp
    core/library/LibraryConfig.cpp
    core/library/DbAccess.cpp
    core/schema/SchemaManager.cpp
    core/library/LibraryService.cpp
    core/creator/CreatorService.cpp
    core/storage/StorageService.cpp
    core/package/PackageService.cpp
    core/import/ImportService.cpp
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
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Sql
)

# ---- jarvis：主程序（组合根 + QML）----
qt_add_executable(jarvis core/main.cpp)

qt_add_qml_module(jarvis
    URI Jarvis
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/dialogs/CreateLibraryDialog.qml
        qml/dialogs/DeleteLibraryDialog.qml
        qml/dialogs/ImportDialog.qml
        qml/windows/ViewerWindow.qml
)

target_link_libraries(jarvis PRIVATE jarvis_core)

# 开发期保留控制台输出；M5 打包时再置 TRUE
set_target_properties(jarvis PROPERTIES WIN32_EXECUTABLE FALSE)

# 构建后把类型包拷贝到产物目录，形成 <exe>/plugins/types/<form>/ 部署结构
add_custom_command(TARGET jarvis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/plugins" "$<TARGET_FILE_DIR:jarvis>/plugins"
)

# ---- 无头冒烟测试（M1/M2 验收用；不依赖界面，直跑核心服务）----
option(JARVIS_BUILD_TESTS "Build Jarvis smoke tests" ON)

if(JARVIS_BUILD_TESTS)
    qt_add_executable(smoke_m1 tests/smoke_m1.cpp)
    target_link_libraries(smoke_m1 PRIVATE jarvis_core)
    set_target_properties(smoke_m1 PROPERTIES WIN32_EXECUTABLE FALSE)

    qt_add_executable(smoke_m2 tests/smoke_m2.cpp)
    target_link_libraries(smoke_m2 PRIVATE jarvis_core)
    set_target_properties(smoke_m2 PROPERTIES WIN32_EXECUTABLE FALSE)

    qt_add_executable(smoke_m3 tests/smoke_m3.cpp)
    target_link_libraries(smoke_m3 PRIVATE jarvis_core)
    set_target_properties(smoke_m3 PROPERTIES WIN32_EXECUTABLE FALSE)

    # 保证类型包已拷贝到产物目录后再运行测试
    add_dependencies(smoke_m1 jarvis)
    add_dependencies(smoke_m2 jarvis)
    add_dependencies(smoke_m3 jarvis)
endif()
