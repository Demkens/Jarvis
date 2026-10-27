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
class SettingsService;
class TagService;
class TypePackage;
class TypePackageManager;

// 类型引擎（开发文档 3.4）：type.json 驱动的 QML 门面。
// 职责：
// - 回答"当前库启用了哪些类型、各自字段/包结构/查看器在哪"（QML 据此渲染）；
// - 包详情读写与创作者绑定（creator 类型经核心 find-or-create，类型包不直写 db）；
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
    // 当前库标签树（扁平 DFS 序：[{id,parent_id,name,depth}]；parent_id=0 为顶级）
    Q_PROPERTY(QVariantList tagTree READ tagTree NOTIFY tagTreeChanged)

public:
    TypeEngine(AppPaths *paths, LibraryService *library, TypePackageManager *typePackages,
               CreatorService *creators, PackageService *packages, TagService *tags,
               EventBus *events, SettingsService *settings, QObject *parent = nullptr);

    QVariantList enabledTypes() const { return m_enabledTypes; }
    QVariantList packages() const { return m_packageList; }
    QVariantList tagTree() const { return m_tagTree; }
    QString currentLibrary() const;

    // 刷新启用类型与包清单（启动、库切换、包事件后调用）
    Q_INVOKABLE void refresh();

    // 刷新标签树缓存（启动、库切换、标签结构变更后调用）
    Q_INVOKABLE void refreshTags();

    // 类型包 QML 文件的 file:// URL（供 Loader 加载）；包缺失返回空 URL
    Q_INVOKABLE QUrl wizardUrl(const QString &form) const;
    Q_INVOKABLE QUrl viewerUrl(const QString &form) const;

    // 制造查看器上下文（每次打开一个；QML 持有期间有效）。
    // 返回 QObject*（运行时实为 PackageViewContext*）而非自定义类型指针——
    // 后者未注册进 QML 元类型系统，引擎会报 Unknown method return type。
    Q_INVOKABLE QObject *createPackageView(qint64 fileId);

    // ---- 数据包详情 ----
    // 读：file 总表列 + 创作者列表 + 封面 URL + 类型 form（见 PackageService::packageDetail）。
    // QML 只能调单参版：AppError* 指针参数无法进入 QML 元方法表，
    // 带指针的重载会被引擎过滤，QML 侧报 "not a function"。
    // 双参版仅供 C++ 内部获取错误信息。
    Q_INVOKABLE QVariantMap packageDetail(qint64 fileId) const;
    QVariantMap packageDetail(qint64 fileId, AppError *error) const;

    // 写创作者多对多：names 为分隔符拼接文本（splitNames 拆分，第一个为主要创作者）；
    // 事务内整体替换该包的 file_creator 行（先删后插）；空文本 → 归为默认创作者「佚名」。
    // 返回 {ok, message?}；成功后广播 PackageUpdated。
    Q_INVOKABLE QVariantMap writeCreators(qint64 fileId, const QString &names);

    // ---- M11a 创作者管理门面（统一返回 {ok, message?, ...}）----
    // 函数: 列出创作者（含别名与关联包数）
    // 输入: keyword —— 同时匹配正式名与别名的子串（空=全部）
    // 输出: {ok, total, items:[{id, name, packageCount, aliases[], system}]}；
    //       system=true 表示系统保留创作者「佚名」（不可重命名、不接纳别名）。
    Q_INVOKABLE QVariantMap listCreators(const QString &keyword) const;

    // 函数: 重命名创作者的正式名
    // 输入: creatorId、name（新正式名）
    // 输出: {ok, message?}；成功后会通知资源列表重查（创作者名展示随之更新）。
    Q_INVOKABLE QVariantMap renameCreator(qint64 creatorId, const QString &name);

    // 函数: 为创作者登记别名
    // 输入: creatorId、alias
    // 输出: {ok, message?}；同一别名已归属其他创作者时拒绝（全库别名唯一），
    //       已归属本创作者时按幂等成功处理。
    Q_INVOKABLE QVariantMap addCreatorAlias(qint64 creatorId, const QString &alias);

    // 函数: 移除创作者的一条别名
    // 输入: creatorId、alias
    // 输出: {ok, message?}；别名不存在也视为成功。
    Q_INVOKABLE QVariantMap removeCreatorAlias(qint64 creatorId, const QString &alias);

    // ---- M4 资源页查询/操作（QML 门面，统一返回 {ok, message?, ...}）----
    // 带条件分页查询：filters 见 PackageService::QueryFilters（键 form/keyword/scope/
    // creationFrom/creationTo/createdFrom/createdTo，缺键即该维度不限；日期键格式 YYYY-MM-DD）；
    // page 从 1 起（每页 M5 设置项）。返回 {ok, total, pages, items[]}（items 含 creatorName/coverUrl）。
    Q_INVOKABLE QVariantMap queryPackages(const QVariantMap &filters, int page);

    // ---- M5 设置门面 ----
    // 返回 {ok, pageSize, scoreRecentWeight, exportDir}（当前内存值）
    Q_INVOKABLE QVariantMap settings() const;

    // 保存：values 键为 pageSize/scoreRecentWeight/exportDir（缺省键保持现值）；
    // 原子写 settings.json 成功后更新内存，并重新应用 setter 到 PackageService
    // （评分权重立即生效；每页数量下次查询即用）。exportDir 为空白串时拒绝并返回失败消息。
    // 返回 {ok, message?}。
    Q_INVOKABLE QVariantMap saveSettings(const QVariantMap &values);

    // 默认导出位置的 file:// URL（供 QML 文件对话框初始定位）；未设置返回空串
    Q_INVOKABLE QString exportDirUrl() const;

    // 改标题 / 评分（0–100，0=清除；二次评分 0.4/0.6 加权）/ 创作日期 / 删除
    Q_INVOKABLE QVariantMap updateTitle(qint64 fileId, const QString &title);
    Q_INVOKABLE QVariantMap ratePackage(qint64 fileId, int score);
    // 创作日期（核心列，格式固定 YYYY-MM-DD）：空串或非法格式返回 {ok:false}
    Q_INVOKABLE QVariantMap updateCreationDate(qint64 fileId, const QString &date);
    Q_INVOKABLE QVariantMap deletePackage(qint64 fileId);

    // 包实体目录绝对路径（"打开实体位置"用）；离线/库未开/包不存在返回空串
    Q_INVOKABLE QString packageDir(qint64 fileId);

    // ---- M6 标签体系门面（统一返回 {ok, message?, ...}；数组按 QML 友好格式）----
    // 新建标签：parentId=0 为顶级；返回 {ok, message?, id}。
    Q_INVOKABLE QVariantMap createTag(int parentId, const QString &name);
    // 重命名标签（不改变层级）
    Q_INVOKABLE QVariantMap renameTag(qint64 tagId, const QString &name);
    // 删除标签（子孙与关联行级联删除）
    Q_INVOKABLE QVariantMap deleteTag(qint64 tagId);
    // 某数据包已绑定的标签 id 列表（升序；库未开/包不存在返回空数组）
    Q_INVOKABLE QVariantList fileTags(qint64 fileId);
    // 整体替换某包的标签集合：tagIds 为 QML 数组（元素为标签 id）。
    // 反链（任意两标签不得互为祖先/后代）由服务层校验，违反即拒绝并返回失败消息。
    Q_INVOKABLE QVariantMap writeFileTags(qint64 fileId, const QVariantList &tagIds);

    // ---- M6 标签树导入/导出门面 ----
    // 导出全部标签到默认导出位置（生成 tags_yyyyMMdd_HHmmss.json）。返回 {ok, message?, path?}；
    // 未设置导出位置或写盘失败时 ok=false。
    Q_INVOKABLE QVariantMap exportTags();

    // 从标签文件（JSON）导入并合并进现有标签树：同层同名复用已有标签并把子标签并入其下（幂等）。
    // path 为标签文件本地路径。返回 {ok, message?, created, merged}；成功后刷新 tagTree。
    Q_INVOKABLE QVariantMap importTags(const QString &path);

    // 包内首个文件（001.ext）的本地 URL（"用系统默认程序打开"用）。
    // 包目录仅含本包文件且按零填充序号命名，系统查看器据此可在同目录内翻页；
    // 离线/库未开/包不存在/无文件返回空 URL。
    Q_INVOKABLE QUrl firstFileUrl(qint64 fileId);

signals:
    void enabledTypesChanged();
    void packagesChanged();
    void tagTreeChanged();

private:
    const TypePackage *enabledPackage(const QString &form) const;
    QVariantList loadEnabledTypes() const;
    QVariantList loadPackages() const;
    QVariantList loadTagTree() const;
    // 给包行附加 creatorName 与 coverUrl（loadPackages / queryPackages 共用）
    QVariantList enrichRows(QVariantList rows) const;

    AppPaths *m_paths;
    LibraryService *m_library;
    TypePackageManager *m_typePackages;
    CreatorService *m_creators;
    PackageService *m_packageService;
    TagService *m_tags;
    EventBus *m_events;
    SettingsService *m_settings;

    QVariantList m_enabledTypes;
    QVariantList m_packageList;
    QVariantList m_tagTree;
};
