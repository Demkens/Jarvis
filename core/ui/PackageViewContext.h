#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class AppPaths;
class LibraryService;
class PackageService;
class TypeEngine;

// 查看器上下文（开发文档 7.3）：TypeEngine 加载 viewer.qml 时注入的唯一对象。
// 类型包 QML 经它访问包数据，不直接开数据库、不直接碰文件系统根：
//   packageId / title / typeForm
//   files：包内文件清单（相对路径、file:// URL、大小），按包内顺序排列
//   fieldValues / writeField：分表字段读写（写操作走核心，触发 PackageUpdated）
//   positionJson / savePosition：浏览记忆（{"index": n} 等，语义由类型自定义）
class PackageViewContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 packageId READ packageId CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY dataChanged)
    Q_PROPERTY(QString typeForm READ typeForm CONSTANT)
    Q_PROPERTY(QVariantList files READ files CONSTANT)
    Q_PROPERTY(QVariantMap fieldValues READ fieldValues NOTIFY fieldsChanged)
    Q_PROPERTY(QString positionJson READ positionJson WRITE setPositionJson
                   NOTIFY positionChanged)

public:
    PackageViewContext(qint64 packageId, AppPaths *paths, LibraryService *library,
                       PackageService *packages, TypeEngine *engine,
                       QObject *parent = nullptr);

    qint64 packageId() const { return m_packageId; }
    QString title() const { return m_title; }
    QString typeForm() const { return m_typeForm; }
    QVariantList files() const { return m_files; }
    QVariantMap fieldValues() const { return m_fields; }

    QString positionJson() const { return m_position; }
    void setPositionJson(const QString &json);

    // 保存浏览位置（查看器翻页/定时调用）；不触发 PackageUpdated
    Q_INVOKABLE bool savePosition();

    // 写分表字段（creator 字段传名字字符串，核心负责 find-or-create）
    Q_INVOKABLE QVariantMap writeField(const QString &name, const QVariant &value);

    // 刷新标题/字段（外部修改后由 TypeEngine 调用）
    void reloadMeta();

signals:
    void dataChanged();
    void fieldsChanged();
    void positionChanged();

private:
    void load();

    qint64 m_packageId;
    AppPaths *m_paths;
    LibraryService *m_library;
    PackageService *m_packages;
    TypeEngine *m_engine;

    QString m_title;
    QString m_typeForm;
    QVariantList m_files;
    QVariantMap m_fields;
    QString m_position;
};
