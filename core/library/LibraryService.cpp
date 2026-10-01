#include "LibraryService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QSaveFile>
#include <QVariantList>

#include "events/EventBus.h"
#include "library/AppPaths.h"
#include "schema/SchemaManager.h"
#include "types/TypePackage.h"
#include "types/TypePackageManager.h"

namespace {

// Windows 文件系统非法字符 + 路径分隔；库名即 envs 下目录名
const QString kIllegalNameChars = QStringLiteral("\\/:*?\"<>|");

// 递归复制目录（导出/导入库时复制 cover、还原库内容用）
bool copyDirectoryRecursively(const QString &srcPath, const QString &destPath)
{
    QDir src(srcPath);
    if (!src.exists())
        return false;
    QDir dest(destPath);
    if (!dest.exists() && !dest.mkpath(QStringLiteral(".")))
        return false;

    for (const QFileInfo &info :
         src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString target = dest.filePath(info.fileName());
        if (info.isDir()) {
            if (!copyDirectoryRecursively(info.absoluteFilePath(), target))
                return false;
        } else if (!QFile::copy(info.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

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
    if (!m_db.open(m_paths->databasePath(entry.name), error))
        return false;
    // 打开既有库时同样补齐核心表到最新版本（与建库路径一致），保证旧库可迁移
    SchemaManager schema;
    return schema.ensureCoreSchema(m_db, error);
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

void LibraryService::startup(const QString &overrideName)
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

    // 多进程入口：--library 指定的库优先；否则恢复上次库
    const QString forced = overrideName.trimmed();
    if (!forced.isEmpty()) {
        if (!m_config.contains(forced)) {
            qWarning().noquote() << "[library] 指定的库不存在，以无库状态启动:" << forced;
            return;
        }
        if (openEntry(m_config.entry(forced), &error)) {
            // 记录为最近打开，供下次无参启动恢复
            m_config.currentName = forced;
            persistConfig(&error);
            setCurrent(forced);
        } else {
            qWarning().noquote() << "[library] 指定的库无法打开，以无库状态启动:" << error.message;
        }
        return;
    }

    const QString last = m_config.currentName;
    if (last.isEmpty() || !m_config.contains(last))
        return;

    if (openEntry(m_config.entry(last), &error)) {
        setCurrent(last);
    } else {
        qWarning().noquote() << "[library] 上次的库无法打开，以无库状态启动:"
                             << error.message;
    }
}

QVariantMap LibraryService::createLibrary(const QString &name,
                                          const QString &linkAddress,
                                          const QStringList &forms)
{
    return createLibraryImpl(name, linkAddress, forms, /*bind=*/true);
}

QVariantMap LibraryService::registerLibrary(const QString &name,
                                            const QString &linkAddress,
                                            const QStringList &forms)
{
    return createLibraryImpl(name, linkAddress, forms, /*bind=*/false);
}

QVariantMap LibraryService::createLibraryImpl(const QString &name,
                                              const QString &linkAddress,
                                              const QStringList &forms,
                                              bool bind)
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

    QString link = AppPaths::normalizeStored(linkAddress.trimmed());
    if (link.isEmpty())
        return failResult(QStringLiteral("链接库目录不能为空"));
    // 锚定绝对路径：相对路径会随进程 CWD 变化，后续导入/删除可能指向错误目录
    if (!QFileInfo(link).isAbsolute())
        link = AppPaths::normalizeStored(QDir::current().filePath(link));

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

    // ---- 3. 失败回滚所需的现场快照（仅 bind 模式需要恢复旧库绑定）----
    const LibraryConfig configBackup = m_config;
    const QString previousName = bind ? m_currentName : QString();
    const LibraryEntry previousEntry = (bind && !previousName.isEmpty())
                                           ? m_config.entry(previousName)
                                           : LibraryEntry{};

    // 建库用连接：bind 复用当前连接；register 用临时连接，不影响已打开的库
    DbAccess tmpDb;
    DbAccess *db = bind ? &m_db : &tmpDb;

    auto cleanup = [&] {
        if (bind)
            m_db.close();
        else
            tmpDb.close();
        QDir(envDir).removeRecursively();
        if (!linkExisted)
            QDir(link).rmdir(QStringLiteral(".")); // 仅当为空时成功，绝不删用户原有数据
        m_config = configBackup;
        if (bind && !previousName.isEmpty())
            openEntry(previousEntry, nullptr);
        if (bind)
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

    AppError error;
    if (!db->open(dbPath, &error)) {
        cleanup();
        return failResult(error);
    }

    // ---- 6. 事务内建核心表 + 各类型分表 ----
    SchemaManager schema;
    if (!schema.ensureCoreSchema(*db, &error)) {
        cleanup();
        return failResult(error);
    }
    for (const QString &form : uniqueForms) {
        const TypePackage *package = m_typePackages->package(form);
        if (!schema.enableTypePackage(*db, *package, &error)) {
            cleanup();
            return failResult(QStringLiteral("类型 %1 启用失败：%2").arg(form, error.message));
        }
    }

    // ---- 7. 写 config.json（bind 模式才切换当前库）----
    LibraryEntry entry;
    entry.name = trimmedName;
    entry.linkAddress = link;
    entry.support = uniqueForms;
    entry.creationDate = QDateTime::currentSecsSinceEpoch();
    entry.schemaVersion = SchemaManager::kCoreSchemaVersion;
    m_config.upsert(entry);
    if (bind)
        m_config.currentName = trimmedName;
    if (!persistConfig(&error)) {
        cleanup();
        return failResult(error);
    }

    // register 模式：临时连接用完即关，m_db 与 currentName 均保持原状
    if (!bind)
        tmpDb.close();

    qInfo().noquote() << "[library]" << (bind ? QStringLiteral("已建库并切换:")
                                              : QStringLiteral("已登记库:"))
                      << trimmedName << "链接库:" << link << "类型:" << uniqueForms;
    if (bind)
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
    if (!persistConfig(&error)) {
        // 持久化失败：回滚内存 currentDb 并重开旧库，避免内存/磁盘不一致且界面误报成功
        m_config.currentName = previousName;
        if (!previousName.isEmpty())
            openEntry(previousEntry, nullptr);
        else
            m_db.close();
        qWarning().noquote() << "[library] currentDb 未能持久化，已回滚:" << error.message;
        return failResult(error);
    }

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

QVariantMap LibraryService::renameLibrary(const QString &oldName, const QString &newName)
{
    const QString old = oldName.trimmed();
    const QString renamed = newName.trimmed();

    if (!m_config.contains(old))
        return failResult(QStringLiteral("库不存在：%1").arg(old));
    // 当前打开的库在此进程持有连接，Windows 下目录被锁，无法改名
    if (old == m_currentName)
        return failResult(QStringLiteral("已打开的库不能重命名：%1").arg(old));

    QString reason;
    if (!isValidLibraryName(renamed, &reason))
        return failResult(reason);
    if (m_config.contains(renamed))
        return failResult(QStringLiteral("已存在同名库：%1").arg(renamed));

    const QString oldDir = m_paths->libraryDir(old);
    const QString newDir = m_paths->libraryDir(renamed);
    if (!QFileInfo::exists(oldDir))
        return failResult(QStringLiteral("数据集目录不存在：%1").arg(QDir::toNativeSeparators(oldDir)));
    if (QFileInfo::exists(newDir))
        return failResult(QStringLiteral("目标目录已存在：%1").arg(QDir::toNativeSeparators(newDir)));

    // 1. 目录重命名（db + cover 随目录整体移动）
    if (!QFile::rename(oldDir, newDir))
        return failResult(QStringLiteral("目录重命名失败（可能被占用）：%1").arg(QDir::toNativeSeparators(oldDir)));

    // 2. db 文件名 <old>.db → <new>.db
    const QString oldDb = newDir + QLatin1Char('/') + old + QStringLiteral(".db");
    const QString newDb = newDir + QLatin1Char('/') + renamed + QStringLiteral(".db");
    if (QFileInfo::exists(oldDb) && !QFile::rename(oldDb, newDb)) {
        QFile::rename(newDir, oldDir); // 回滚目录
        return failResult(QStringLiteral("数据库文件重命名失败：%1").arg(QDir::toNativeSeparators(oldDb)));
    }

    // 3. 登记更新 + 持久化；失败回滚文件系统与内存
    const LibraryConfig configBackup = m_config;
    m_config.rename(old, renamed);
    AppError error;
    if (!persistConfig(&error)) {
        m_config = configBackup;
        if (QFileInfo::exists(newDb))
            QFile::rename(newDb, oldDb);
        QFile::rename(newDir, oldDir);
        return failResult(error);
    }

    emit librariesChanged();
    qInfo().noquote() << "[library] 已重命名库:" << old << "→" << renamed;
    return okResult();
}

QVariantMap LibraryService::exportLibrary(const QString &name, const QString &destDir)
{
    const QString trimmed = name.trimmed();
    if (!m_config.contains(trimmed))
        return failResult(QStringLiteral("库不存在：%1").arg(trimmed));

    const QString dest = QDir::cleanPath(destDir.trimmed());
    if (dest.isEmpty())
        return failResult(QStringLiteral("导出目标目录不能为空"));

    QDir destRoot(dest);
    if (!destRoot.exists() && !destRoot.mkpath(QStringLiteral(".")))
        return failResult(QStringLiteral("导出目标目录无法创建：%1").arg(QDir::toNativeSeparators(dest)));

    const QString targetDir = destRoot.filePath(trimmed);
    if (QFileInfo::exists(targetDir))
        return failResult(QStringLiteral("导出目标已存在：%1").arg(QDir::toNativeSeparators(targetDir)));
    if (!QDir().mkpath(targetDir))
        return failResult(QStringLiteral("导出目录创建失败：%1").arg(QDir::toNativeSeparators(targetDir)));

    auto rollback = [&] { QDir(targetDir).removeRecursively(); };

    // 导出当前库时先 checkpoint，把 WAL 内容落盘到主 db，保证副本一致
    if (trimmed == m_currentName && m_db.isOpen())
        m_db.execute(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));

    // 1. 复制 db 文件
    const QString srcDb = m_paths->databasePath(trimmed);
    const QString dstDb = targetDir + QLatin1Char('/') + trimmed + QStringLiteral(".db");
    if (!QFile::copy(srcDb, dstDb)) {
        rollback();
        return failResult(QStringLiteral("数据库复制失败：%1").arg(QDir::toNativeSeparators(srcDb)));
    }

    // 2. 复制 cover/ 目录（早期库可能无封面，缺失则跳过）
    const QString srcCover = m_paths->coverDir(trimmed);
    if (QFileInfo::exists(srcCover)
        && !copyDirectoryRecursively(srcCover, targetDir + QStringLiteral("/cover"))) {
        rollback();
        return failResult(QStringLiteral("封面目录复制失败：%1").arg(QDir::toNativeSeparators(srcCover)));
    }

    // 3. 写清单（导入恢复 name/linkAddress/support 等登记信息）
    const LibraryEntry entry = m_config.entry(trimmed);
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("jarvis-library"));
    manifest.insert(QStringLiteral("name"), entry.name);
    manifest.insert(QStringLiteral("linkAddress"), entry.linkAddress);
    QJsonArray support;
    for (const QString &form : entry.support)
        support.append(form);
    manifest.insert(QStringLiteral("support"), support);
    manifest.insert(QStringLiteral("creationDate"), static_cast<double>(entry.creationDate));
    manifest.insert(QStringLiteral("schemaVersion"), entry.schemaVersion);

    QSaveFile manifestFile(targetDir + QStringLiteral("/library.json"));
    if (!manifestFile.open(QIODevice::WriteOnly)
        || manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
        || !manifestFile.commit()) {
        rollback();
        return failResult(QStringLiteral("清单写入失败：%1").arg(QDir::toNativeSeparators(targetDir)));
    }

    qInfo().noquote() << "[library] 已导出库:" << trimmed << "→" << QDir::toNativeSeparators(targetDir);
    return okResult();
}

QVariantMap LibraryService::importLibrary(const QString &sourceDir)
{
    const QString src = QDir::cleanPath(sourceDir.trimmed());
    if (src.isEmpty())
        return failResult(QStringLiteral("待导入目录不能为空"));

    const QDir srcDir(src);
    if (!srcDir.exists())
        return failResult(QStringLiteral("待导入目录不存在：%1").arg(QDir::toNativeSeparators(src)));

    // 1. 读清单
    const QString manifestPath = srcDir.filePath(QStringLiteral("library.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly))
        return failResult(QStringLiteral("找不到清单文件：%1").arg(QDir::toNativeSeparators(manifestPath)));
    const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
    manifestFile.close();
    if (!doc.isObject())
        return failResult(QStringLiteral("清单不是合法 JSON：%1").arg(QDir::toNativeSeparators(manifestPath)));

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("format"))
        && root.value(QStringLiteral("format")).toString() != QStringLiteral("jarvis-library"))
        return failResult(QStringLiteral("清单格式不受支持：%1")
                              .arg(root.value(QStringLiteral("format")).toString()));

    const QString name = root.value(QStringLiteral("name")).toString().trimmed();
    QString reason;
    if (!isValidLibraryName(name, &reason))
        return failResult(reason);
    if (m_config.contains(name))
        return failResult(QStringLiteral("已存在同名库：%1").arg(name));

    const QString linkAddress = root.value(QStringLiteral("linkAddress")).toString().trimmed();
    if (linkAddress.isEmpty())
        return failResult(QStringLiteral("清单缺少链接库目录"));

    QStringList support;
    const QJsonArray supportArr = root.value(QStringLiteral("support")).toArray();
    for (const QJsonValue &form : supportArr) {
        const QString f = form.toString().trimmed();
        if (!f.isEmpty() && !support.contains(f))
            support.append(f);
    }
    for (const QString &form : support) {
        if (m_typePackages->package(form) == nullptr)
            return failResult(QStringLiteral("类型包未安装：%1").arg(form));
    }

    if (!m_paths->ensureRoot())
        return failResult(QStringLiteral("数据目录不可用：%1").arg(m_paths->envsRoot()));

    const QString targetDir = m_paths->libraryDir(name);
    if (QFileInfo::exists(targetDir))
        return failResult(QStringLiteral("数据集目录已存在：%1").arg(QDir::toNativeSeparators(targetDir)));
    QDir target(targetDir);
    if (!target.mkpath(QStringLiteral(".")))
        return failResult(QStringLiteral("数据集目录创建失败：%1").arg(QDir::toNativeSeparators(targetDir)));

    auto rollback = [&] { QDir(targetDir).removeRecursively(); };

    // 2. 复制除清单外的全部条目（db + cover），失败整体回滚
    const QDir src2(src);
    for (const QFileInfo &info :
         src2.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (info.fileName() == QStringLiteral("library.json"))
            continue;
        const QString targetPath = target.filePath(info.fileName());
        const bool ok = info.isDir()
                            ? copyDirectoryRecursively(info.absoluteFilePath(), targetPath)
                            : QFile::copy(info.absoluteFilePath(), targetPath);
        if (!ok) {
            rollback();
            return failResult(QStringLiteral("复制失败：%1").arg(info.absoluteFilePath()));
        }
    }

    // 3. 校验 db 确实存在（清单有但文件缺失则失败，避免登记后打不开）
    const QString dbPath = m_paths->databasePath(name);
    if (!QFileInfo::exists(dbPath)) {
        rollback();
        return failResult(QStringLiteral("导入包缺少数据库文件：%1").arg(QDir::toNativeSeparators(dbPath)));
    }

    // 4. 登记（与 registerLibrary 一致：不绑定当前库、不改 currentName）
    const LibraryConfig configBackup = m_config;
    LibraryEntry entry;
    entry.name = name;
    entry.linkAddress = AppPaths::normalizeStored(linkAddress);
    entry.support = support;
    entry.creationDate = static_cast<qint64>(root.value(QStringLiteral("creationDate")).toDouble(0));
    entry.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
    m_config.upsert(entry);

    AppError error;
    if (!persistConfig(&error)) {
        m_config = configBackup;
        rollback();
        return failResult(error);
    }

    emit librariesChanged();
    qInfo().noquote() << "[library] 已导入库:" << name << "来源:" << QDir::toNativeSeparators(src);
    return okResult();
}

QVariantMap LibraryService::launchLibrary(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_config.contains(trimmed))
        return failResult(QStringLiteral("库不存在：%1").arg(trimmed));

    const QString program = QCoreApplication::applicationFilePath();
    if (!QProcess::startDetached(program, {QStringLiteral("--library"), trimmed}))
        return failResult(QStringLiteral("无法启动库进程：%1").arg(program));

    qInfo().noquote() << "[library] 已拉起库进程:" << trimmed;
    return okResult();
}
