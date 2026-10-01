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

    // 启动：读取 config.json。overrideName 非空（多进程 --library）时直接打开该库；
    // 为空则恢复上次 currentDb（db 文件缺失时不打开并告警）。
    void startup(const QString &overrideName = {});

    Q_INVOKABLE QVariantMap createLibrary(const QString &name,
                                          const QString &linkAddress,
                                          const QStringList &forms);
    // 建库但不绑定本进程（多进程库管理界面用）：不切换 m_db、不改 currentName。
    Q_INVOKABLE QVariantMap registerLibrary(const QString &name,
                                            const QString &linkAddress,
                                            const QStringList &forms);
    Q_INVOKABLE QVariantMap switchLibrary(const QString &name);
    Q_INVOKABLE QVariantMap deleteLibrary(const QString &name, bool deleteEntityFiles);
    // 重命名库（当前打开的库会被拒绝）：重命名 envs/<old> 目录与内部 <old>.db 文件，
    // 并保序更新 config。返回 {ok, message}。
    Q_INVOKABLE QVariantMap renameLibrary(const QString &oldName, const QString &newName);
    // 导出内嵌数据集到 destDir/<name>/（db + cover + library.json 清单）。
    Q_INVOKABLE QVariantMap exportLibrary(const QString &name, const QString &destDir);
    // 导入之前导出的库目录（含 library.json）：拷入 envs 并登记，不改当前库。
    Q_INVOKABLE QVariantMap importLibrary(const QString &sourceDir);
    // 拉起新进程打开指定库（多进程模型：每库一窗口）。
    Q_INVOKABLE QVariantMap launchLibrary(const QString &name);

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

    // 建库通用实现：bind=false 用临时连接建 schema 且不绑定 m_db / currentName
    // （registerLibrary 用）；bind=true 保留单进程切换语义（createLibrary/smoke 用）。
    QVariantMap createLibraryImpl(const QString &name, const QString &linkAddress,
                                  const QStringList &forms, bool bind);

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
