#include "types/TypeEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QUrl>

#include "creator/CreatorService.h"
#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "library/DbAccess.h"
#include "library/LibraryService.h"
#include "package/PackageService.h"
#include "settings/SettingsService.h"
#include "storage/StorageService.h"
#include "tag/TagService.h"
#include "ui/PackageViewContext.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

TypeEngine::TypeEngine(AppPaths *paths, LibraryService *library, TypePackageManager *typePackages,
                       CreatorService *creators, PackageService *packages, TagService *tags,
                       EventBus *events, SettingsService *settings, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_library(library)
    , m_typePackages(typePackages)
    , m_creators(creators)
    , m_packageService(packages)
    , m_tags(tags)
    , m_events(events)
    , m_settings(settings)
{
    // 库切换/包增删改 → 刷新 QML 可见模型
    QObject::connect(m_events, &EventBus::librarySwitched, this, [this](const QString &) {
        refresh();
        refreshTags();
    });
    QObject::connect(m_events, &EventBus::packageCreated, this,
                     [this](qint64, const QString &) { refresh(); });
    QObject::connect(m_events, &EventBus::packageDeleted, this, [this](qint64) { refresh(); });
    QObject::connect(m_events, &EventBus::packageUpdated, this, [this](qint64) { refresh(); });

    refresh();
    refreshTags();
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

        list.append(QVariantMap{
            {QStringLiteral("form"), package->form},
            {QStringLiteral("displayName"), package->displayName},
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

// 标签树（当前库）；库未开返回空表（标签全库共享，与类型无关）
QVariantList TypeEngine::loadTagTree() const
{
    if (!m_library->isOpen())
        return {};
    AppError error;
    const QVariantList tree = m_tags->tree(&error);
    if (!error.ok()) {
        qWarning().noquote() << "[tags] 标签树读取失败:" << error.message;
        return {};
    }
    return tree;
}

void TypeEngine::refreshTags()
{
    m_tagTree = loadTagTree();
    emit tagTreeChanged();
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
    // 标签筛选：QML 传标签 id 数组（缺键/空数组=不限标签）；tagMatchAll 控制 AND/OR
    const QVariantList tagIds = filters.value(QStringLiteral("tagIds")).toList();
    for (const QVariant &value : tagIds) {
        bool ok = false;
        const qint64 tagId = value.toLongLong(&ok);
        if (ok && tagId > 0)
            query.tagIds.append(tagId);
    }
    query.tagMatchAll = filters.value(QStringLiteral("tagMatchAll")).toBool();

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

QVariantMap TypeEngine::createTag(int parentId, const QString &name)
{
    qint64 newId = 0;
    AppError error;
    if (!m_tags->createTag(parentId, name, &newId, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    refreshTags();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), newId}};
}

QVariantMap TypeEngine::renameTag(qint64 tagId, const QString &name)
{
    AppError error;
    if (!m_tags->renameTag(tagId, name, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    refreshTags();
    return {{QStringLiteral("ok"), true}};
}

QVariantMap TypeEngine::deleteTag(qint64 tagId)
{
    AppError error;
    if (!m_tags->deleteTag(tagId, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    refreshTags();
    // 标签删除会级联清理 file_tag，可能改变当前筛选结果 → 通知资源列表重查
    emit packagesChanged();
    return {{QStringLiteral("ok"), true}};
}

QVariantList TypeEngine::fileTags(qint64 fileId)
{
    QVariantList out;
    if (!m_library->isOpen())
        return out;
    AppError error;
    const QList<qint64> ids = m_tags->tagsOfFile(fileId, &error);
    if (!error.ok()) {
        qWarning().noquote() << "[tags] 读取包标签失败:" << error.message;
        return out;
    }
    for (qint64 id : ids)
        out.append(id);
    return out;
}

QVariantMap TypeEngine::writeFileTags(qint64 fileId, const QVariantList &tagIds)
{
    QList<qint64> ids;
    for (const QVariant &value : tagIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id > 0)
            ids.append(id);
    }

    AppError error;
    if (!m_tags->setFileTags(fileId, ids, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
    // 绑定变化可能改变标签筛选结果 → 通知资源列表重查
    emit packagesChanged();
    return {{QStringLiteral("ok"), true}};
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

QVariantMap TypeEngine::packageDetail(qint64 fileId) const
{
    // QML 入口：无错误指针，失败时 QML 侧按空对象处理
    return packageDetail(fileId, nullptr);
}

QVariantMap TypeEngine::packageDetail(qint64 fileId, AppError *error) const
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
    auto *context = new PackageViewContext(fileId, m_paths, m_library, m_packageService, nullptr);
    QQmlEngine::setObjectOwnership(context, QQmlEngine::JavaScriptOwnership);
    return context;
}

QVariantMap TypeEngine::writeCreators(qint64 fileId, const QString &names)
{
    // 数据包存在性校验（复用详情读取；文件不存在/库未开时 detail 为空）
    AppError error;
    const QVariantMap detail = packageDetail(fileId, &error);
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
