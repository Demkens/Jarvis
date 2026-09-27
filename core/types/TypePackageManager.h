#pragma once

#include <QObject>
#include <QVariantList>
#include <memory>
#include <vector>

#include "TypePackage.h"

// 类型包管理器：发现、校验、加载 plugins/types/ 下的类型包，维护注册表。
// 职责边界：只管"包怎么进来"（发现/校验/注册）；包"怎么用"归 TypeEngine（M3）。
class TypePackageManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList loadedPackages READ loadedPackages CONSTANT)

public:
    explicit TypePackageManager(QObject *parent = nullptr);
    ~TypePackageManager() override;

    // 扫描候选目录，加载全部合法类型包；非法包跳过并记日志。返回成功加载数。
    int discoverAndLoad();

    const TypePackage *package(const QString &form) const;
    QList<const TypePackage *> packages() const;

    // 供 QML 展示：[{form, displayName, version}, ...]
    QVariantList loadedPackages() const;

private:
    // 类型包搜索根，按优先级排序：可执行文件旁（部署/构建拷贝）→ 源码目录（开发期回退）
    static QStringList candidateRoots();

    // 校验并加载单个包目录；失败记日志并返回 false（不阻断其余包）
    bool loadOne(const QString &dirPath);

    // 类型数量极小（规划共 7 类），线性查找足够；
    // 用 std::vector 而非 QList：规避 COW detach 对 move-only 元素的要求
    std::vector<std::unique_ptr<TypePackage>> m_packages;
};
