#include "types/TypeEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QQmlEngine>
#include <QSet>
#include <QUrl>

#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "library/DbAccess.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "settings/SettingsService.h"
#include "storage/StorageService.h"
#include "ui/PackageViewContext.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

TypeEngine::TypeEngine(AppPaths *paths, LibraryService *library, TypePackageManager *typePackages,
                       CreatorService *creators, PackageService *packages,
                       EventBus *events, SettingsService *settings, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_library(library)
    , m_typePackages(typePackages)
    , m_creators(creators)
    , m_packageService(packages)
    , m_events(events)
    , m_settings(settings)
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
    if (!error.ok()) { // DB 故障：记日志而非静默当成"未启用"
        qWarning().noquote() << "[types] enabledPackage 查询失败:" << error.message;
        return nullptr;
    }
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
    if (!error.ok()) { // DB 故障：记日志而非静默返回空列表
        qWarning().noquote() << "[types] loadEnabledTypes 查询失败:" << error.message;
        return list;
    }
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
    return enrichRows(rows);
}

// 给包行附加创作者显示名与封面 URL（资源页/查询共用；封面常驻 envs，离线也可显示）
QVariantList TypeEngine::enrichRows(QVariantList rows) const
{
    const QString libraryName = m_library->currentName();
    for (QVariant &item : rows) {
        QVariantMap row = item.toMap();
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        const int bucket = row.value(QStringLiteral("storage_path")).toInt();

        AppError detailError;
        const QVariantMap detail = m_packageService->packageDetail(id, &detailError);
        if (detailError.ok() && detail.contains(QStringLiteral("creatorName")))
            row.insert(QStringLiteral("creatorName"), detail.value(QStringLiteral("creatorName")));
        if (detailError.ok() && detail.contains(QStringLiteral("creatorCount")))
            row.insert(QStringLiteral("creatorCount"), detail.value(QStringLiteral("creatorCount")));

        const QString coverAbs = StorageService::coverPath(
            m_paths->coverDir(libraryName), bucket, id);
        row.insert(QStringLiteral("coverUrl"),
                   QFileInfo::exists(coverAbs) ? QUrl::fromLocalFile(coverAbs) : QUrl());
        item = row;
    }
    return rows;
}

QVariantMap TypeEngine::queryPackages(const QVariantMap &filters, int page)
{
    // QML 只提交「有值的条件」；这里逐一取用并做空值兜底（缺键=该维度不限）
    PackageService::QueryFilters query;
    query.form = filters.value(QStringLiteral("form")).toString();
    query.keyword = filters.value(QStringLiteral("keyword")).toString();
    query.scope = filters.value(QStringLiteral("scope")).toInt();
    query.creationFrom = filters.value(QStringLiteral("creationFrom")).toString();
    query.creationTo = filters.value(QStringLiteral("creationTo")).toString();
    query.createdFrom = filters.value(QStringLiteral("createdFrom")).toString();
    query.createdTo = filters.value(QStringLiteral("createdTo")).toString();

    const int pageSize = m_settings->pageSize(); // M5 全局设置"每页数量"
    const int pageNum = qMax(1, page);
    const int offset = (pageNum - 1) * pageSize;

    AppError error;
    int total = 0;
    QVariantList rows = m_packageService->queryPackages(query, offset, pageSize, &total, &error);
    if (!error.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};

    const int pages = (total + pageSize - 1) / pageSize;
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("total"), total},
            {QStringLiteral("pages"), pages},
            {QStringLiteral("items"), enrichRows(rows)}};
}

QVariantMap TypeEngine::settings() const
{
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("pageSize"), m_settings->pageSize()},
            {QStringLiteral("scoreRecentWeight"), m_settings->scoreRecentWeight()}};
}

QVariantMap TypeEngine::saveSettings(const QVariantMap &values)
{
    // 在现值基础上只覆盖传入键：QML 侧始终传全量键，但这里不假设调用方传全
    SettingsService next = *m_settings;
    if (values.contains(QStringLiteral("pageSize")))
        next.setPageSize(values.value(QStringLiteral("pageSize")).toInt());
    if (values.contains(QStringLiteral("scoreRecentWeight")))
        next.setScoreRecentWeight(values.value(QStringLiteral("scoreRecentWeight")).toInt());

    AppError error;
    if (!next.save(SettingsService::defaultPath(m_paths), &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};

    // 落盘成功后才更新内存与各服务（避免写盘失败时状态不一致）
    *m_settings = next;
    m_packageService->setScoreRecentWeight(next.scoreRecentWeight() / 100.0);
    return {{QStringLiteral("ok"), true}};
}

QVariantMap TypeEngine::updateTitle(qint64 fileId, const QString &title)
{
    AppError error;
    if (m_packageService->updateTitle(fileId, title, &error))
        return {{QStringLiteral("ok"), true}};
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
}

QVariantMap TypeEngine::ratePackage(qint64 fileId, int score)
{
    AppError error;
    if (m_packageService->ratePackage(fileId, score, &error))
        return {{QStringLiteral("ok"), true}};
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
}

QVariantMap TypeEngine::updateCreationDate(qint64 fileId, const QString &date)
{
    AppError error;
    if (m_packageService->updateCreationDate(fileId, date, &error))
        return {{QStringLiteral("ok"), true}};
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
}

QVariantMap TypeEngine::deletePackage(qint64 fileId)
{
    AppError error;
    if (m_packageService->deletePackage(fileId, &error))
        return {{QStringLiteral("ok"), true}};
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
}

QString TypeEngine::packageDir(qint64 fileId)
{
    if (!m_library->isOpen())
        return QString();

    const QString libraryName = m_library->currentName();
    AppError error;
    const QVariantMap detail = m_packageService->packageDetail(fileId, &error);
    if (!error.ok() || detail.isEmpty())
        return QString();

    const int bucket = detail.value(QStringLiteral("storage_path")).toInt();
    const QString linkAddress = m_library->config().entry(libraryName).linkAddress;
    return StorageService::packageDir(linkAddress, bucket, fileId);
}

QUrl TypeEngine::firstFileUrl(qint64 fileId)
{
    const QString dirPath = packageDir(fileId);
    if (dirPath.isEmpty())
        return QUrl();

    // 按文件名升序取第一个（零填充序号保证 001→002…，即包内首帧）
    const QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    if (entries.isEmpty())
        return QUrl();
    return QUrl::fromLocalFile(entries.first().absoluteFilePath());
}

QVariantMap TypeEngine::readFields(qint64 fileId) const
{
    // QML 入口：无错误指针，失败时 QML 侧按空对象处理
    return readFields(fileId, nullptr);
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

QObject *TypeEngine::createPackageView(qint64 fileId)
{
    AppError error;
    const QVariantMap detail = m_packageService->packageDetail(fileId, &error);
    if (!error.ok() || detail.isEmpty())
        return nullptr;

    // parent 传 nullptr + JavaScriptOwnership：生命周期完全归 QML（GC 回收），
    // 避免 C++ parent 链与 JS 所有权并存导致的退出期 use-after-free。
    auto *context = new PackageViewContext(fileId, m_paths, m_library, m_packageService, this,
                                           nullptr);
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
    // 拼 SQL 前的纵深防御：表名必须通过安全校验（与 StorageService 其余拼接点一致）
    if (!StorageService::isSafeFormName(form))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("类型名非法：%1").arg(form)}};
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

        if (declaredType == QStringLiteral("date")) {
            // 空串 → NULL（未设置）；非空须为严格 YYYY-MM-DD（v5 起统一精度）
            const QString text = stored.toString().trimmed();
            if (text.isEmpty()) {
                stored = QVariant();
            } else if (!StorageService::isValidCreationDate(text)) {
                return {{QStringLiteral("ok"), false},
                        {QStringLiteral("message"),
                         QStringLiteral("日期格式须为 YYYY-MM-DD：%1").arg(text)}};
            }
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

QVariantMap TypeEngine::writeCreators(qint64 fileId, const QString &names)
{
    // 数据包存在性校验（复用详情读取；文件不存在/库未开时 detail 为空）
    AppError error;
    const QVariantMap detail = readFields(fileId, &error);
    if (!error.ok() || detail.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"),
                 error.ok() ? QStringLiteral("数据包不存在：#%1").arg(fileId) : error.message}};

    // 拆分 + 逐个 find-or-create（顺序即 sort_order，第一个为主要创作者）；
    // 空输入 → 归为默认创作者「佚名」（不产生空关联，保证始终可检索）
    QList<qint64> ids;
    const QStringList namesList = CreatorService::splitNames(names);
    if (namesList.isEmpty()) {
        const qint64 id = m_creators->resolveOrCreate(QString(), CreatorService::Person, &error);
        if (id < 0)
            return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
        ids.append(id);
    } else {
        for (const QString &name : namesList) {
            const qint64 id = m_creators->resolveOrCreate(name, CreatorService::Person, &error);
            if (id < 0)
                return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
            ids.append(id);
        }
    }

    // 事务内整体替换该包的关联行（空输入已归为「佚名」，故不会清空）
    DbAccess &db = m_library->dbAccess();
    if (!db.transaction(&error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};

    if (!db.execute(QStringLiteral("DELETE FROM file_creator WHERE file_id = :file_id"),
                    {{QStringLiteral("file_id"), fileId}}, &error)) {
        db.rollback();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    }
    for (int index = 0; index < ids.size(); ++index) {
        if (!db.execute(QStringLiteral(
                            "INSERT INTO file_creator (file_id, creator_id, sort_order) "
                            "VALUES (:file_id, :creator_id, :sort_order)"),
                        {{QStringLiteral("file_id"), fileId},
                         {QStringLiteral("creator_id"), ids.at(index)},
                         {QStringLiteral("sort_order"), index}},
                        &error)) {
            db.rollback();
            return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
        }
    }
    if (!db.commit(&error)) {
        db.rollback();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    }

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
