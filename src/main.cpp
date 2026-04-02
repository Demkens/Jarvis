#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "dbmodel.h"
#include "cppobj.h"
#include "filemodel.h"
#include "labelmodel.h"
#include "datacountermodel.h"
#include "docmodel.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    // 创建应用程序实例
    QGuiApplication app(argc, argv);

    // 创建QML引擎
    QQmlApplicationEngine engine;

    // 注册C++类型到QML
    // DatabaseModel: 数据库管理模型（核心数据库操作）
    qmlRegisterType<DatabaseModel>("Model", 1, 0, "DatabaseModel");

    // FileModel: 文件数据模型（文件CRUD操作）
    qmlRegisterType<FileModel>("Model", 1, 0, "FileModel");

    // LabelModel: 标签数据模型（标签管理）
    qmlRegisterType<LabelModel>("Model", 1, 0, "LabelModel");

    // DataCounterModel: 数据统计模型（统计分析）
    qmlRegisterType<DataCounterModel>("Model", 1, 0, "DataCounterModel");

    // DocModel: 文档数据模型（文档管理）
    qmlRegisterType<DocModel>("Model", 1, 0, "DocModel");

    // 加载主QML文件
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));

    // 监听QML对象创建，确保主窗口加载成功
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    // 加载QML资源
    engine.load(url);

    // 进入事件循环
    return app.exec();
}

