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
               CreatorService *creators, PackageService *packages, EventBus *events,
               QObject *parent = nullptr);

    QVariantList enabledTypes() const { return m_enabledTypes; }
    QVariantList packages() const { return m_packageList; }
    QString currentLibrary() const;

    // 刷新启用类型与包清单（启动、库切换、包事件后调用）
    Q_INVOKABLE void refresh();

    // 类型包 QML 文件的 file:// URL（供 Loader 加载）；包缺失返回空 URL
    Q_INVOKABLE QUrl wizardUrl(const QString &form) const;
    Q_INVOKABLE QUrl viewerUrl(const QString &form) const;

    // 制造查看器上下文（每次打开一个；QML 持有期间有效）
    Q_INVOKABLE PackageViewContext *createPackageView(qint64 fileId);

    // ---- 分表通用 CRUD ----
    // 读：file 行之外的分表字段（含 creatorName 解析结果）
    QVariantMap readFields(qint64 fileId, AppError *error = nullptr) const;

    // 写：values 的键为字段名；creator 字段可传名字（find-or-create）或数字 id；
    // date 传空串置 NULL。成功后广播 PackageUpdated。
    QVariantMap writeFields(qint64 fileId, const QVariantMap &values);

signals:
    void enabledTypesChanged();
    void packagesChanged();

private:
    const TypePackage *enabledPackage(const QString &form) const;
    QVariantList loadEnabledTypes() const;
    QVariantList loadPackages() const;

    AppPaths *m_paths;
    LibraryService *m_library;
    TypePackageManager *m_typePackages;
    CreatorService *m_creators;
    PackageService *m_packageService;
    EventBus *m_events;

    QVariantList m_enabledTypes;
    QVariantList m_packageList;
};
