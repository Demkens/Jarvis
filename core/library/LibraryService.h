#pragma once

#include <QObject>
#include <QVariantList>

#include "common/AppError.h"
#include "library/DbAccess.h"
#include "library/LibraryConfig.h"

class AppPaths;
class EventBus;
class TypePackageManager;

// 库服务（开发文档 3.4 / 4.3）：库列表、当前库、创建/切换/删除、类型启用。
// QML 经 Q_INVOKABLE 调用，结果统一返回 {ok: bool, message: string}，
// C++ 内部使用 AppError。
class LibraryService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged)
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)

public:
    LibraryService(AppPaths *paths,
                   TypePackageManager *typePackages,
                   EventBus *eventBus,
                   QObject *parent = nullptr);

    // 启动：读取 config.json，自动恢复 currentDb（db 文件缺失时不打开并告警）
    void startup();

    Q_INVOKABLE QVariantMap createLibrary(const QString &name,
                                          const QString &linkAddress,
                                          const QStringList &forms);
    Q_INVOKABLE QVariantMap switchLibrary(const QString &name);
    Q_INVOKABLE QVariantMap deleteLibrary(const QString &name, bool deleteEntityFiles);

    QString currentName() const { return m_currentName; }
    bool isOpen() const { return m_db.isOpen(); }
    QVariantList libraries() const;

    // C++ 内部（测试/后续服务）使用
    DbAccess &dbAccess() { return m_db; }
    const LibraryConfig &config() const { return m_config; }

signals:
    void currentChanged();
    void librariesChanged();
    void openChanged();

private:
    static bool isValidLibraryName(const QString &name, QString *reason);
    bool openEntry(const LibraryEntry &entry, AppError *error);
    bool persistConfig(AppError *error);
    void setCurrent(const QString &name);

    static QVariantMap okResult();
    static QVariantMap failResult(const AppError &error);
    static QVariantMap failResult(const QString &message);

    AppPaths *m_paths;
    TypePackageManager *m_typePackages;
    EventBus *m_eventBus;

    LibraryConfig m_config;
    DbAccess m_db;
    QString m_currentName;
};
