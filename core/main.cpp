#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "app/Application.h"

// 组合根：main.cpp 是唯一装配点 —— 创建核心服务、注入 QML 上下文。
// QML 不自行构造数据模型（废弃 v1 的散点 qmlRegisterType）。
// 多进程模型：--library <name> 直接打开指定库作为本进程主窗口；无参数时入口进程
// 恢复上次库（无库则进入库管理门禁窗口）。
int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Jarvis"));
    QCoreApplication::setApplicationVersion(QStringLiteral(JARVIS_CORE_VERSION));

    // 解析 --library <name>：指定本进程打开的库
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Jarvis 本地资源管理"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption libraryOption(
        QStringLiteral("library"),
        QStringLiteral("打开指定库（库名即 envs 下目录名）"),
        QStringLiteral("name"));
    parser.addOption(libraryOption);
    parser.process(app);

    Application application;
    application.initialize(parser.value(libraryOption));

    QQmlApplicationEngine engine;
    application.bindToQml(engine);
    engine.loadFromModule("Jarvis", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return QGuiApplication::exec();
}
