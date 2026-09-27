#include "TypePackageManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "TypePackage.h"

namespace {

// 版本比较：x.y.z -> 逐段数值比较。a < b 返回负数，相等返回 0
int compareVersion(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < 3; ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb)
            return va - vb;
    }
    return 0;
}

} // namespace

TypePackageManager::TypePackageManager(QObject *parent)
    : QObject(parent)
{
}

TypePackageManager::~TypePackageManager() = default;

QStringList TypePackageManager::candidateRoots()
{
    return {
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/types"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/plugins/types"),
    };
}

int TypePackageManager::discoverAndLoad()
{
    m_packages.clear();

    // 取第一个存在的搜索根（部署结构优先，源码目录为开发期回退）
    QString root;
    const QStringList candidates = candidateRoots();
    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists()) {
            root = candidate;
            break;
        }
    }
    if (root.isEmpty()) {
        qWarning().noquote() << "[types] 未找到类型包目录，已搜索：" << candidates;
        return 0;
    }
    qInfo().noquote() << "[types] 类型包搜索根:" << QDir::toNativeSeparators(root);

    const QDir typesDir(root);
    const QFileInfoList entries = typesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                         QDir::Name);
    for (const QFileInfo &entry : entries)
        loadOne(entry.absoluteFilePath());

    // 汇总日志（M0 完成标志）
    if (m_packages.empty()) {
        qInfo() << "[types] 未加载任何类型包";
    } else {
        QStringList names;
        names.reserve(m_packages.size());
        for (const auto &pkg : m_packages) {
            names << QStringLiteral("%1(%2) v%3").arg(pkg->form, pkg->displayName)
                         .arg(pkg->version);
        }
        qInfo().noquote() << "[types] 已加载" << m_packages.size()
                          << "个类型包:" << names.join(QStringLiteral(", "));
    }
    return static_cast<int>(m_packages.size());
}

bool TypePackageManager::loadOne(const QString &dirPath)
{
    const QDir dir(dirPath);
    const QString formHint = dir.dirName();

    // 1. 四个契约文件必须齐备
    const QString typeJsonPath = dir.absoluteFilePath(QStringLiteral("type.json"));
    const QString migratePath  = dir.absoluteFilePath(QStringLiteral("migrate.sql"));
    const QString wizardPath   = dir.absoluteFilePath(QStringLiteral("wizard.qml"));
    const QString viewerPath   = dir.absoluteFilePath(QStringLiteral("viewer.qml"));
    for (const QString &path : {typeJsonPath, migratePath, wizardPath, viewerPath}) {
        if (!QFileInfo::exists(path)) {
            qWarning().noquote() << "[types]" << formHint << "缺少契约文件，跳过:"
                                 << QFileInfo(path).fileName();
            return false;
        }
    }

    // 2. type.json 可解析为 JSON 对象
    QFile file(typeJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[types]" << formHint << "type.json 读取失败，跳过";
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning().noquote() << "[types]" << formHint << "type.json 不是合法 JSON 对象，跳过";
        return false;
    }

    // 3. 契约内容校验（字段定义、版本、包内规范等）
    auto pkg = std::make_unique<TypePackage>();
    QString error;
    if (!TypePackage::fromJson(doc.object(), *pkg, error)) {
        qWarning().noquote() << "[types]" << formHint << "type.json 校验失败，跳过:" << error;
        return false;
    }

    // 4. 目录名应与 form 一致（避免部署歧义）
    if (formHint != pkg->form) {
        qWarning().noquote() << "[types] 目录名" << formHint << "与 form" << pkg->form
                             << "不一致，跳过";
        return false;
    }

    // 5. 核心版本兼容：核心须 >= 包声明的 coreMinVersion
    if (compareVersion(QStringLiteral(JARVIS_CORE_VERSION), pkg->coreMinVersion) < 0) {
        qWarning().noquote() << "[types]" << pkg->form << "要求核心版本 >=" << pkg->coreMinVersion
                             << "，当前" << QStringLiteral(JARVIS_CORE_VERSION) << "，跳过";
        return false;
    }

    // 6. form 唯一性
    if (package(pkg->form) != nullptr) {
        qWarning().noquote() << "[types] form 重复注册，跳过:" << pkg->form;
        return false;
    }

    // 7. 登记路径并入库
    pkg->dirPath = dirPath;
    pkg->migratePath = migratePath;
    pkg->wizardPath = wizardPath;
    pkg->viewerPath = viewerPath;
    m_packages.push_back(std::move(pkg));
    return true;
}

const TypePackage *TypePackageManager::package(const QString &form) const
{
    for (const auto &pkg : m_packages) {
        if (pkg->form == form)
            return pkg.get();
    }
    return nullptr;
}

QList<const TypePackage *> TypePackageManager::packages() const
{
    QList<const TypePackage *> result;
    result.reserve(m_packages.size());
    for (const auto &pkg : m_packages)
        result.append(pkg.get());
    return result;
}

QVariantList TypePackageManager::loadedPackages() const
{
    QVariantList list;
    list.reserve(m_packages.size());
    for (const auto &pkg : m_packages) {
        QVariantMap item;
        item.insert(QStringLiteral("form"), pkg->form);
        item.insert(QStringLiteral("displayName"), pkg->displayName);
        item.insert(QStringLiteral("version"), pkg->version);
        list.append(item);
    }
    return list;
}
