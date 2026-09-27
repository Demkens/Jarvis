#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "app/Application.h"

// 组合根：main.cpp 是唯一装配点 —— 创建核心服务、注入 QML 上下文。
// QML 不自行构造数据模型（废弃 v1 的散点 qmlRegisterType）。
int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Jarvis"));
    QCoreApplication::setApplicationVersion(QStringLiteral(JARVIS_CORE_VERSION));

    Application application;
    application.initialize();

    QQmlApplicationEngine engine;
    application.bindToQml(engine);
    engine.loadFromModule("Jarvis", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return QGuiApplication::exec();
}
