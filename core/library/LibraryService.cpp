#include "LibraryService.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantList>

#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "schema/SchemaManager.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

namespace {

// Windows 文件系统非法字符 + 路径分隔；库名即 envs 下目录名
const QString kIllegalNameChars = QStringLiteral("\\/:*?\"<>|");

} // namespace

LibraryService::LibraryService(AppPaths *paths,
                               TypePackageManager *typePackages,
                               EventBus *eventBus,
                               QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_typePackages(typePackages)
    , m_eventBus(eventBus)
{
}

bool LibraryService::isValidLibraryName(const QString &name, QString *reason)
{
    if (name.isEmpty()) {
        if (reason != nullptr) *reason = QStringLiteral("库名不能为空");
        return false;
    }
    if (name.size() > 64) {
        if (reason != nullptr) *reason = QStringLiteral("库名不能超过 64 个字符");
        return false;
    }
    if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
        if (reason != nullptr) *reason = QStringLiteral("库名不能为 . 或 ..");
        return false;
    }
    for (const QChar ch : name) {
        const ushort u = ch.unicode();
        const bool isControl = (u < 0x20 || u == 0x7F);
        if (kIllegalNameChars.contains(ch) || isControl) {
            if (reason != nullptr)
                *reason = QStringLiteral("库名含非法字符：%1").arg(ch);
            return false;
        }
    }
    return true;
}

QVariantMap LibraryService::okResult()
{
    return {{QStringLiteral("ok"), true}};
}

QVariantMap LibraryService::failResult(const AppError &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error.message}};
}

QVariantMap LibraryService::failResult(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

bool LibraryService::persistConfig(AppError *error)
{
    return m_config.save(m_paths->configPath(), error);
}

bool LibraryService::openEntry(const LibraryEntry &entry, AppError *error)
{
    return m_db.open(m_paths->databasePath(entry.name), error);
}

void LibraryService::setCurrent(const QString &name)
{
    if (m_currentName == name)
        return;
    m_currentName = name;
    emit currentChanged();
    emit openChanged();
    if (m_eventBus != nullptr)
        emit m_eventBus->librarySwitched(name);
}

QVariantList LibraryService::libraries() const
{
    QVariantList list;
    for (const LibraryEntry &entry : m_config.entriesInOrder()) {
        QVariantList support;
        for (const QString &form : entry.support)
            support.append(form);
        list.append(QVariantMap{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("linkAddress"), entry.linkAddress},
            {QStringLiteral("support"), support},
            {QStringLiteral("creationDate"), entry.creationDate},
            {QStringLiteral("schemaVersion"), entry.schemaVersion},
            {QStringLiteral("isCurrent"), entry.name == m_currentName},
        });
    }
    return list;
}

void LibraryService::startup()
{
    AppError error;
    if (!m_paths->ensureRoot(&error)) {
        qWarning().noquote() << "[library]" << error.message;
        return;
    }
    if (!m_config.load(m_paths->configPath(), &error)) {
        qWarning().noquote() << "[library]" << error.message;
        return;
    }

    const QString name = m_config.currentName;
    if (name.isEmpty() || !m_config.contains(name))
        return;

    if (openEntry(m_config.entry(name), &error)) {
        setCurrent(name);
    } else {
        qWarning().noquote() << "[library] 上次的库无法打开，以无库状态启动:"
                             << error.message;
    }
}

QVariantMap LibraryService::createLibrary(const QString &name,
                                          const QString &linkAddress,
                                          const QStringList &forms)
{
    // ---- 1. 输入校验（开发文档 4.3 步骤 1）----
    const QString trimmedName = name.trimmed();
    QString reason;
    if (!isValidLibraryName(trimmedName, &reason))
        return failResult(reason);

    QStringList uniqueForms;
    for (const QString &rawForm : forms) {
        const QString form = rawForm.trimmed();
        if (form.isEmpty())
            continue;
        if (m_typePackages->package(form) == nullptr)
            return failResult(QStringLiteral("类型包未安装或未通过校验：%1").arg(form));
        if (!uniqueForms.contains(form))
            uniqueForms.append(form);
    }

    const QString link = AppPaths::normalizeStored(linkAddress.trimmed());
    if (link.isEmpty())
        return failResult(QStringLiteral("链接库目录不能为空"));

    if (!m_paths->ensureRoot())
        return failResult(QStringLiteral("数据目录不可用：%1").arg(m_paths->envsRoot()));

    if (m_config.contains(trimmedName))
        return failResult(QStringLiteral("已存在同名库：%1").arg(trimmedName));

    const QString envDir = m_paths->libraryDir(trimmedName);
    const QString dbPath = m_paths->databasePath(trimmedName);
    if (QFileInfo::exists(envDir))
        return failResult(QStringLiteral("数据集目录已存在：%1").arg(QDir::toNativeSeparators(envDir)));

    // ---- 2. 链接库目录（存在或可创建）----
    QDir linkDir(link);
    const bool linkExisted = linkDir.exists();
    if (!linkExisted && !linkDir.mkpath(QStringLiteral(".")))
        return failResult(QStringLiteral("链接库目录无法创建：%1").arg(QDir::toNativeSeparators(link)));
    const QFileInfo linkInfo(link);
    if (!linkInfo.exists() || !linkInfo.isDir())
        return failResult(QStringLiteral("链接库路径不是文件夹：%1").arg(QDir::toNativeSeparators(link)));

    // ---- 3. 失败回滚所需的现场快照 ----
    const LibraryConfig configBackup = m_config;
    const QString previousName = m_currentName;
    const LibraryEntry previousEntry = previousName.isEmpty() ? LibraryEntry{}
                                                              : m_config.entry(previousName);

    auto cleanup = [&] {
        m_db.close();
        QDir(envDir).removeRecursively();
        if (!linkExisted)
            QDir(link).rmdir(QStringLiteral(".")); // 仅当为空时成功，绝不删用户原有数据
        m_config = configBackup;
        if (!previousName.isEmpty())
            openEntry(previousEntry, nullptr);
        setCurrent(previousName);
    };

    // ---- 4. 内嵌数据集目录 envs/<库名>/ 与 cover/ ----
    if (!QDir().mkpath(m_paths->coverDir(trimmedName))) {
        cleanup();
        return failResult(QStringLiteral("数据集目录创建失败：%1").arg(QDir::toNativeSeparators(envDir)));
    }

    // ---- 5. 建空 db 文件后由 DbAccess 打开（DbAccess 不允许打开不存在的文件，防误建）----
    {
        QFile dbFile(dbPath);
        if (!dbFile.open(QIODevice::WriteOnly)) {
            cleanup();
            return failResult(QStringLiteral("数据库文件创建失败：%1").arg(QDir::toNativeSeparators(dbPath)));
        }
        dbFile.close();
    }

    // 切换连接到新库（旧连接在 DbAccess::open 内关闭）
    AppError error;
    if (!m_db.open(dbPath, &error)) {
        cleanup();
        return failResult(error);
    }

    // ---- 6. 事务内建核心表 + 各类型分表 ----
    SchemaManager schema;
    if (!schema.ensureCoreSchema(m_db, &error)) {
        cleanup();
        return failResult(error);
    }
    for (const QString &form : uniqueForms) {
        const TypePackage *package = m_typePackages->package(form);
        if (!schema.enableTypePackage(m_db, *package, &error)) {
            cleanup();
            return failResult(QStringLiteral("类型 %1 启用失败：%2").arg(form, error.message));
        }
    }

    // ---- 7. 写 config.json 并切换当前库 ----
    LibraryEntry entry;
    entry.name = trimmedName;
    entry.linkAddress = link;
    entry.support = uniqueForms;
    entry.creationDate = QDateTime::currentSecsSinceEpoch();
    entry.schemaVersion = SchemaManager::kCoreSchemaVersion;
    m_config.upsert(entry);
    m_config.currentName = trimmedName;
    if (!persistConfig(&error)) {
        cleanup();
        return failResult(error);
    }

    qInfo().noquote() << "[library] 已建库并切换:" << trimmedName
                      << "链接库:" << link << "类型:" << uniqueForms;
    setCurrent(trimmedName);
    emit librariesChanged();

    QVariantMap result = okResult();
    result.insert(QStringLiteral("name"), trimmedName);
    return result;
}

QVariantMap LibraryService::switchLibrary(const QString &name)
{
    if (!m_config.contains(name))
        return failResult(QStringLiteral("库不存在：%1").arg(name));
    if (m_currentName == name)
        return okResult();

    const QString previousName = m_currentName;
    const LibraryEntry previousEntry = previousName.isEmpty() ? LibraryEntry{}
                                                              : m_config.entry(previousName);
    const LibraryEntry target = m_config.entry(name);

    if (!QFileInfo::exists(m_paths->databasePath(name)))
        return failResult(QStringLiteral("数据库文件缺失，无法切换：%1").arg(name));

    AppError error;
    if (!openEntry(target, &error)) {
        if (!previousName.isEmpty())
            openEntry(previousEntry, nullptr);
        return failResult(error);
    }

    m_config.currentName = name;
    if (!persistConfig(&error))
        qWarning().noquote() << "[library] currentDb 未能持久化:" << error.message;

    setCurrent(name);
    qInfo().noquote() << "[library] 已切换到:" << name;
    return okResult();
}

QVariantMap LibraryService::deleteLibrary(const QString &name, bool deleteEntityFiles)
{
    if (!m_config.contains(name))
        return failResult(QStringLiteral("库不存在：%1").arg(name));

    const LibraryEntry entry = m_config.entry(name);

    // 1. 先删实体文件（失败则整体取消，db 与登记均保持原状）
    if (deleteEntityFiles && !entry.linkAddress.isEmpty()) {
        QDir linkDir(entry.linkAddress);
        if (linkDir.exists() && !linkDir.removeRecursively()) {
            return failResult(QStringLiteral("实体文件删除失败（可能被占用或权限不足），已取消：%1")
                                  .arg(QDir::toNativeSeparators(entry.linkAddress)));
        }
    }

    // 2. 先关闭指向该库的连接（Windows 下打开中的 db 文件无法删除）
    if (m_currentName == name)
        m_db.close();

    // 3. 删内嵌数据集（db/WAL/封面随目录一起清除）
    const QString envDir = m_paths->libraryDir(name);
    if (QFileInfo::exists(envDir) && !QDir(envDir).removeRecursively()) {
        // 删除失败：若有顺延库则重新打开，保持工作状态
        if (m_currentName == name) {
            const QString fallback = m_config.createdDb.isEmpty() ? QString()
                                                                  : m_config.createdDb.first();
            if (!fallback.isEmpty() && fallback != name)
                openEntry(m_config.entry(fallback), nullptr);
        }
        return failResult(QStringLiteral("数据集删除失败：%1").arg(QDir::toNativeSeparators(envDir)));
    }

    // 4. 更新登记；remove() 会把 currentDb 顺延为首个剩余库或清空
    m_config.remove(name);
    AppError error;
    if (!persistConfig(&error))
        return failResult(error);

    const QString nextCurrent = m_config.currentName;
    if (m_currentName == name && !nextCurrent.isEmpty())
        openEntry(m_config.entry(nextCurrent), nullptr);
    setCurrent(nextCurrent);
    emit librariesChanged();

    qInfo().noquote() << "[library] 已删除库:" << name
                      << (deleteEntityFiles ? QStringLiteral("（含实体文件）")
                                            : QStringLiteral("（保留实体文件）"));
    return okResult();
}
