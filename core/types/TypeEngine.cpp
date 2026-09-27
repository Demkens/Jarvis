#include "types/TypeEngine.h"

#include <QFileInfo>
#include <QHash>
#include <QQmlEngine>
#include <QSet>
#include <QUrl>

#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "storage/StorageService.h"
#include "ui/PackageViewContext.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

TypeEngine::TypeEngine(AppPaths *paths, LibraryService *library, TypePackageManager *typePackages,
                       CreatorService *creators, PackageService *packages, EventBus *events,
                       QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_library(library)
    , m_typePackages(typePackages)
    , m_creators(creators)
    , m_packageService(packages)
    , m_events(events)
{
    // 库切换/包增删改 → 刷新 QML 可见模型
    QObject::connect(m_events, &EventBus::librarySwitched, this, [this](const QString &) {
        refresh();
    });
    QObject::connect(m_events, &EventBus::packageCreated, this,
                     [this](qint64, const QString &) { refresh(); });
    QObject::connect(m_events, &EventBus::packageDeleted, this, [this](qint64) { refresh(); });
    QObject::connect(m_events, &EventBus::packageUpdated, this, [this](qint64) { refresh(); });

    refresh();
}

QString TypeEngine::currentLibrary() const
{
    return m_library->currentName();
}

const TypePackage *TypeEngine::enabledPackage(const QString &form) const
{
    // 须同时满足：库已打开、db 已启用、类型包文件在位
    if (!m_library->isOpen())
        return nullptr;
    AppError error;
    const int count = m_library->dbAccess()
                          .scalar(QStringLiteral("SELECT count(*) FROM type WHERE form = :form"),
                                  {{QStringLiteral("form"), form}}, &error)
                          .toInt();
    if (count != 1)
        return nullptr;
    return m_typePackages->package(form);
}

QVariantList TypeEngine::loadEnabledTypes() const
{
    QVariantList list;
    if (!m_library->isOpen())
        return list;

    AppError error;
    const QList<QVariantMap> rows = m_library->dbAccess().select(
        QStringLiteral("SELECT form FROM type ORDER BY id"), {}, &error);
    for (const QVariantMap &row : rows) {
        const QString form = row.value(QStringLiteral("form")).toString();
        const TypePackage *package = m_typePackages->package(form);
        if (package == nullptr)
            continue; // 类型包文件缺失：数据只读隐藏（开发文档 4.4）

        QVariantList fields;
        for (const TypePackage::Field &field : package->fields) {
            fields.append(QVariantMap{
                {QStringLiteral("name"), field.name},
                {QStringLiteral("type"), field.type},
                {QStringLiteral("label"), field.label},
                {QStringLiteral("required"), field.required},
                {QStringLiteral("defaultValue"), field.defaultValue},
            });
        }
        list.append(QVariantMap{
            {QStringLiteral("form"), package->form},
            {QStringLiteral("displayName"), package->displayName},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("layoutKind"), package->packageLayoutKind},
            {QStringLiteral("browseOrder"), package->browseOrder},
        });
    }
    return list;
}

QVariantList TypeEngine::loadPackages() const
{
    if (!m_library->isOpen())
        return {};

    AppError error;
    QVariantList rows = m_packageService->listPackages(QString(), &error);
    if (!error.ok())
        return {};

    const QString libraryName = m_library->currentName();
    for (QVariant &item : rows) {
        QVariantMap row = item.toMap();
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        const int bucket = row.value(QStringLiteral("storage_path")).toInt();

        // 合并创作者显示名（M4 资源页直接可用）
        AppError detailError;
        const QVariantMap detail = m_packageService->packageDetail(id, &detailError);
        if (detailError.ok() && detail.contains(QStringLiteral("creatorName")))
            row.insert(QStringLiteral("creatorName"), detail.value(QStringLiteral("creatorName")));

        // 封面常驻 envs（离线也可显示），URL 由核心拼接
        const QString coverAbs = StorageService::coverPath(
            m_paths->coverDir(libraryName), bucket, id);
        row.insert(QStringLiteral("coverUrl"),
                   QFileInfo::exists(coverAbs) ? QUrl::fromLocalFile(coverAbs) : QUrl());
        item = row;
    }
    return rows;
}

QVariantMap TypeEngine::readFields(qint64 fileId, AppError *error) const
{
    return m_packageService->packageDetail(fileId, error);
}

QUrl TypeEngine::wizardUrl(const QString &form) const
{
    const TypePackage *package = enabledPackage(form);
    return package == nullptr ? QUrl() : QUrl::fromLocalFile(package->wizardPath);
}

QUrl TypeEngine::viewerUrl(const QString &form) const
{
    const TypePackage *package = enabledPackage(form);
    return package == nullptr ? QUrl() : QUrl::fromLocalFile(package->viewerPath);
}

PackageViewContext *TypeEngine::createPackageView(qint64 fileId)
{
    AppError error;
    const QVariantMap detail = m_packageService->packageDetail(fileId, &error);
    if (!error.ok() || detail.isEmpty())
        return nullptr;

    auto *context = new PackageViewContext(fileId, m_paths, m_library, m_packageService, this,
                                           this);
    // QML 侧 Loader/Window 持有；JS 所有权，引用消失后 GC
    QQmlEngine::setObjectOwnership(context, QQmlEngine::JavaScriptOwnership);
    return context;
}

QVariantMap TypeEngine::writeFields(qint64 fileId, const QVariantMap &values)
{
    AppError error;
    const QVariantMap detail = m_packageService->packageDetail(fileId, &error);
    if (!error.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    if (detail.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("数据包不存在：#%1").arg(fileId)}};

    const QString form = detail.value(QStringLiteral("type_form")).toString();
    const TypePackage *package = enabledPackage(form);
    if (package == nullptr)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("类型包缺失，字段只读：%1").arg(form)}};

    // 分表实有列（防写不存在的列）
    const QList<QVariantMap> columns = m_library->dbAccess().select(
        QStringLiteral("PRAGMA table_info(%1)").arg(form), {}, &error);
    if (!error.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    QSet<QString> actualColumns;
    for (const QVariantMap &column : columns)
        actualColumns.insert(column.value(QStringLiteral("name")).toString());

    // 字段名 → 字段类型（type.json 声明）
    QHash<QString, QString> declaredTypes;
    for (const TypePackage::Field &field : package->fields)
        declaredTypes.insert(field.name, field.type);
    QStringList setClauses;
    QVariantMap params{{QStringLiteral("id"), fileId}};
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString name = it.key();
        if (name == QStringLiteral("id"))
            continue;
        if (!actualColumns.contains(name))
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("message"), QStringLiteral("字段不存在：%1").arg(name)}};

        const QString declaredType = declaredTypes.value(name);
        QVariant stored = it.value();

        if (declaredType == QStringLiteral("creator")) {
            // 传名字 → find-or-create；传数字 → 直接当 id
            bool numeric = false;
            const qint64 asId = stored.toLongLong(&numeric);
            if (numeric && asId >= 0) {
                stored = asId;
            } else {
                const qint64 resolved = m_creators->resolveOrCreate(stored.toString(),
                                                                    CreatorService::Person,
                                                                    &error);
                if (resolved < 0)
                    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
                stored = resolved;
            }
        } else if (declaredType == QStringLiteral("date")) {
            // 空串 → NULL（未知日期）；非空须满足 YYYY / YYYY-MM / YYYY-MM-DD
            const QString text = stored.toString().trimmed();
            if (!StorageService::isValidCreationDate(text))
                return {{QStringLiteral("ok"), false},
                        {QStringLiteral("message"),
                         QStringLiteral("创作日期格式须为 YYYY / YYYY-MM / YYYY-MM-DD：%1").arg(text)}};
            if (text.isEmpty())
                stored = QVariant();
        }

        setClauses.append(QStringLiteral("%1 = :%1").arg(name));
        params.insert(name, stored);
    }

    if (setClauses.isEmpty())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("message"), QStringLiteral("无字段需要写入")}};

    if (!m_library->dbAccess().execute(
            QStringLiteral("UPDATE %1 SET %2 WHERE id = :id")
                .arg(form, setClauses.join(QStringLiteral(", "))),
            params, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};

    emit m_events->packageUpdated(fileId);
    return {{QStringLiteral("ok"), true}};
}

void TypeEngine::refresh()
{
    m_enabledTypes = loadEnabledTypes();
    m_packageList = loadPackages();
    emit enabledTypesChanged();
    emit packagesChanged();
}
