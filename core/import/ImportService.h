#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "storage/StorageService.h"

class LibraryService;
class TypePackageManager;

// 导入/导出 QML 门面（开发文档 5.4 / 5.5 / 7.4）。
// 向导与预处理两通道都归一为 ImportPlan 映射数组，由本服务批量执行；
// 预处理目录扫描（draw 约定：子文件夹=图集包、根目录散图=单图包、meta.json 可选）在此。
// 纪律继承 StorageService：核心独占文件系统写操作；失败条目单独报告不阻断其余。
class ImportService : public QObject
{
    Q_OBJECT

public:
    ImportService(LibraryService *library, StorageService *storage,
                  TypePackageManager *typePackages, QObject *parent = nullptr);

    // 执行一批计划。返回：
    // {ok: 是否全部成功, total, success, failed,
    //  results: [{index, ok, id?, message?}], importedIds: [...]}
    Q_INVOKABLE QVariantMap executePlans(const QString &form, const QVariantList &plans);

    // 预处理扫描。返回：
    // {ok, plans: [ImportPlan 映射...], message}
    // 计划的标题/创作者/日期允许 QML 在确认页批量补填后再提交
    Q_INVOKABLE QVariantMap scanPreprocess(const QString &form, const QString &rootDir);

    // 导出包到已存在的空目录
    Q_INVOKABLE QVariantMap exportPackage(qint64 id, const QString &destDir);

    // 当前实际库是否在线（QML 据此禁用导入/导出入口）
    Q_INVOKABLE bool isOnline() const;

private:
    static bool isImageFile(const QString &fileName);
    static ImportPlan planFromMap(const QString &form, const QVariantMap &map);

    LibraryService *m_library;
    StorageService *m_storage;
    TypePackageManager *m_typePackages;
};
