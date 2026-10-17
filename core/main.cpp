// 软件唯一入口: 
// 1. 最先安装日志模块
// 2. 解析命令行参数`--library <库名>`
// 3. 调用`Application::initialize()`装配全部服务, 调用`loadFromModule()`拉起界面
 
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "app/Application.h"
#include "common/Logging.h"
#include "library/AppPaths.h"

int main(int argc, char *argv[])
{   
    // 设置软件运行环境
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);    // 高 DPI 舍入策略
    QCoreApplication::setApplicationName(QStringLiteral("Jarvis"));     // 给应用取名
    QCoreApplication::setApplicationVersion(QStringLiteral(JARVIS_CORE_VERSION));   // 登记核心版本号
    QGuiApplication app(argc, argv);    // 创建应用对象

    // 安装日志模块：让所有的初始化诊断(含类型包发现、库打开)都写入日志文件
    Logging::install(AppPaths::defaultEnvsRoot());

    // 解析命令行参数 --library <name>：指定本进程打开的库
    QCommandLineParser parser;          // 创建命令行参数解析器
    parser.setApplicationDescription(QStringLiteral("Jarvis 本地资源管理"));
    parser.addHelpOption();             // 注册`-h / --help`
    parser.addVersionOption();          // 注册`-v / --version`
    QCommandLineOption libraryOption(   // 声明名为`library` 的自定义选项
        QStringLiteral("library"),
        QStringLiteral("打开指定库（库名即 envs 下目录名）"),
        QStringLiteral("name"));
    parser.addOption(libraryOption);    // 把选项登记进解析器
    parser.process(app);                // 开始解析应用对象的命令行参数

    // 调用`Application::initialize()`装配全部服务, 调用`loadFromModule()`拉起界面
    Application application;                    // 组合根，全程序唯一创建并组装所有服务的地方
    application.initialize(parser.value(libraryOption));    // 按这个库名装配全部服务
    QQmlApplicationEngine engine;               // 构建QML引擎, 承载类型系统与所有QML根对象
    application.bindToQml(engine);              // 把4个服务注入QML上下文(`libraryService``typePackageManager``typeEngine``importService`)
    engine.loadFromModule("Jarvis", "Main");    // 指定QML的入口文件为Jarvis的Main.qml
    if (engine.rootObjects().isEmpty())         // 装载失败检测
        return -1;

    return QGuiApplication::exec();             // 启动事件循环并阻塞 ，直到退出条件满足
}
