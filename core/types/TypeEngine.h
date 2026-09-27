#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "common/AppError.h"

class AppPaths;
class CreatorService;
class EventBus;
class LibraryService;
class PackageService;
class PackageViewContext;
class SettingsService;
class StorageService;
class TypePackage;
class TypePackageManager;

// 类型引擎（开发文档 3.4）：type.json 驱动的 QML 门面。
// 职责：
// - 回答"当前库启用了哪些类型、各自字段/包结构/查看器在哪"（QML 据此渲染）；
// - 分表字段的通用读写（creator 类型经核心 find-or-create，类型包不直写 db）；
// - 加载类型包 wizard.qml / viewer.qml 并为查看器制造上下文对象；
// - 维护当前库包清单（供 M3 简易列表与 M4 资源页共用）。
// 边界：不拥有连接（借图书馆长的 DbAccess）；不碰实体文件（导入导出归 ImportService）。
class TypeEngine : public QObject
{
    Q_OBJECT
    // 当前库已启用且类型包在位的类型；随库切换/包事件刷新
    Q_PROPERTY(QVariantList enabledTypes READ enabledTypes NOTIFY enabledTypesChanged)
    Q_PROPERTY(QVariantList packages READ packages NOTIFY packagesChanged)
    Q_PROPERTY(QString currentLibrary READ currentLibrary NOTIFY enabledTypesChanged)

public:
    TypeEngine(AppPaths *paths, LibraryService *library, TypePackageManager *typePackages,
               CreatorService *creators, PackageService *packages, StorageService *storage,
               EventBus *events, SettingsService *settings, QObject *parent = nullptr);

    QVariantList enabledTypes() const { return m_enabledTypes; }
    QVariantList packages() const { return m_packageList; }
    QString currentLibrary() const;

    // 刷新启用类型与包清单（启动、库切换、包事件后调用）
    Q_INVOKABLE void refresh();

    // 类型包 QML 文件的 file:// URL（供 Loader 加载）；包缺失返回空 URL
    Q_INVOKABLE QUrl wizardUrl(const QString &form) const;
    Q_INVOKABLE QUrl viewerUrl(const QString &form) const;

    // 制造查看器上下文（每次打开一个；QML 持有期间有效）。
    // 返回 QObject*（运行时实为 PackageViewContext*）而非自定义类型指针——
    // 后者未注册进 QML 元类型系统，引擎会报 Unknown method return type。
    Q_INVOKABLE QObject *createPackageView(qint64 fileId);

    // ---- 分表通用 CRUD ----
    // 读：file 行之外的分表字段（含 creatorName 解析结果）。
    // QML 只能调单参版：AppError* 指针参数无法进入 QML 元方法表，
    // 带指针的重载会被引擎过滤，QML 侧报 "not a function"。
    // 双参版仅供 C++ 内部/测试获取错误信息。
    Q_INVOKABLE QVariantMap readFields(qint64 fileId) const;
    QVariantMap readFields(qint64 fileId, AppError *error) const;

    // 写：values 的键为字段名；creator 字段可传名字（find-or-create）或数字 id；
    // date 传空串置 NULL。成功后广播 PackageUpdated。
    Q_INVOKABLE QVariantMap writeFields(qint64 fileId, const QVariantMap &values);

    // ---- M4 资源页查询/操作（QML 门面，统一返回 {ok, message?, ...}）----
    // 带条件分页查询：form 空=全部类型；keyword 标题模糊搜索；page 从 1 起（每页 M5 设置项）。
    // 返回 {ok, total, pages, items[]}，items 含 creatorName/coverUrl（与 packages 同款拼接）。
    Q_INVOKABLE QVariantMap queryPackages(const QString &form, const QString &keyword, int page);

    // ---- M5 设置门面 ----
    // 返回 {ok, pageSize, coverLongEdge, scoreRecentWeight}（当前内存值）
    Q_INVOKABLE QVariantMap settings() const;

    // 保存：values 键为 pageSize/coverLongEdge/scoreRecentWeight（缺省键保持现值）；
    // 原子写 settings.json 成功后更新内存，并重新应用 setter 到 StorageService/PackageService
    // （封面长边、评分权重立即生效；每页数量下次查询即用）。返回 {ok, message?}。
    Q_INVOKABLE QVariantMap saveSettings(const QVariantMap &values);

    // 改标题 / 评分（0–100，0=清除；二次评分 0.4/0.6 加权）/ 删除 / 重生成封面
    Q_INVOKABLE QVariantMap updateTitle(qint64 fileId, const QString &title);
    Q_INVOKABLE QVariantMap ratePackage(qint64 fileId, int score);
    Q_INVOKABLE QVariantMap deletePackage(qint64 fileId);
    Q_INVOKABLE QVariantMap regenerateCover(qint64 fileId);

    // 包实体目录绝对路径（"打开实体位置"用）；离线/库未开/包不存在返回空串
    Q_INVOKABLE QString packageDir(qint64 fileId);

signals:
    void enabledTypesChanged();
    void packagesChanged();

private:
    const TypePackage *enabledPackage(const QString &form) const;
    QVariantList loadEnabledTypes() const;
    QVariantList loadPackages() const;
    // 给包行附加 creatorName 与 coverUrl（loadPackages / queryPackages 共用）
    QVariantList enrichRows(QVariantList rows) const;

    AppPaths *m_paths;
    LibraryService *m_library;
    TypePackageManager *m_typePackages;
    CreatorService *m_creators;
    PackageService *m_packageService;
    StorageService *m_storage;
    EventBus *m_events;
    SettingsService *m_settings;

    QVariantList m_enabledTypes;
    QVariantList m_packageList;
};
