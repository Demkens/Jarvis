#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "storage/StorageService.h"

class DedupService;
class LibraryService;
class TypePackage;
class TypePackageManager;

// 导入/导出 QML 门面
// 向导与预处理两通道都归一为 ImportPlan 映射数组，由本服务批量执行；
// 按类型包声明的 packageLayout.kind 分派执行策略：
//  - imageSequence（draw/comic）：所选图片按序号命名落位，导入前做图片完全重复预检；
//  - wholeBookText（novel）：整本 txt 先经 BookSplitter 切分为章节文件与 config.txt，
//    再按条目落位；文本不产生图片指纹，故不做查重预检；
//  - singleMedia（film/modelfilm）：每包恰好 1 个媒体文件（保留原文件名），
//    扩展名须落在类型声明的 media 内；视频不参与图片查重，无图片时生成占位封面；
//  - mediaSequence：文件夹 → 包（包内媒体按文件名排序），根目录散文件各自成包。
// 预处理目录扫描同样按 kind 分派（draw 扫子文件夹与散图，novel 扫每个 .txt，
// singleMedia 递归扫每个视频，mediaSequence 扫一层文件夹与散文件）。
// 纪律继承 StorageService：核心独占文件系统写操作；失败条目单独报告不阻断其余。
class ImportService : public QObject
{
    Q_OBJECT

public:
    ImportService(LibraryService *library, StorageService *storage,
                  TypePackageManager *typePackages, DedupService *dedup,
                  QObject *parent = nullptr);

    // 执行一批计划。返回：
    // {ok: 是否全部成功, total, success, failed,
    //  results: [{index, ok, id?, message?, chapters?,
    //             duplicate?, duplicates?: [{path, size, existing: [...]}]}],
    //  importedIds: [...]}
    // 每个计划在复制前做完全重复预检（仅图片类型）；命中的计划不导入，其余计划继续执行。
    Q_INVOKABLE QVariantMap executePlans(const QString &form, const QVariantList &plans);

    // 预处理扫描。返回：
    // {ok, plans: [ImportPlan 映射...], warnings: [...]}
    // 计划字段随布局类型不同：图片/视频类型为 files（源媒体清单），
    // 文本类型为 sourcePath（整本 txt）；确认页可批量补填标题/创作者/日期后提交。
    Q_INVOKABLE QVariantMap scanPreprocess(const QString &form, const QString &rootDir);

    // 导入前预览（通道A 左侧只读元数据用）。返回：
    // {ok, id?, count, size, message?}
    // id = 预计的下一个自增主键（SELECT MAX(id)+1，单用户场景即实际将分配的 id）；
    // count/size = 所选文件数与总字节数（QML 无法 stat 文件，故在核心算）。
    Q_INVOKABLE QVariantMap previewImport(const QVariantList &files);

    // 整本小说分析（向导页实时预览用）。返回：
    // {ok, encoding, ruleId, chapterCount, firstTitle, lastTitle, warnings: [...], message?}
    // 只读文件、不落库、不写盘；result.warnings 与 message 区分：前者不阻断导入。
    Q_INVOKABLE QVariantMap analyzeBook(const QString &form, const QString &path);

    // 预处理约定提示文案（按类型包布局返回，QML 不再硬编码）
    Q_INVOKABLE QString preprocessHint(const QString &form) const;

    // 导出包到已存在的空目录
    Q_INVOKABLE QVariantMap exportPackage(qint64 id, const QString &destDir);

    // 当前实际库是否在线（QML 据此禁用导入/导出入口）
    Q_INVOKABLE bool isOnline() const;

private:
    // 计划映射 → ImportPlan（entries 规整：显式条目优先，否则把 files 简写为条目；
    // 命名规则随布局不同：singleMedia 保留原文件名，其余按序号命名）
    static ImportPlan planFromMap(const QString &form, const TypePackage *package,
                                  const QVariantMap &map);
    // 媒体类计划（imageSequence / singleMedia / mediaSequence）：
    // 校验媒体类别与包内文件数 → 图片完全重复预检 → 复制导入 → 写指纹
    QVariantMap executeMediaPlan(const QString &form, const QVariantMap &map, int index);
    // 文本类计划：切分到临时目录 → 生成条目 → 复制导入 → 清理临时目录
    QVariantMap executeBookPlan(const QString &form, const QVariantMap &map, int index);
    // 图片序列预处理扫描（每个子文件夹/根目录散图各一个计划）
    QVariantMap scanImagePreprocess(const QString &rootDir);
    // 单媒体预处理扫描（递归扫描根目录，每个受支持的媒体文件 = 一个包）
    QVariantMap scanSingleMediaPreprocess(const TypePackage *package, const QString &rootDir);
    // 多媒体序列预处理扫描（根目录一层文件夹 = 包，散文件各自成包）
    QVariantMap scanMediaSequencePreprocess(const TypePackage *package, const QString &rootDir);
    // 小说预处理扫描（每个 .txt 一个计划）
    QVariantMap scanBookPreprocess(const QString &form, const QString &rootDir);
    // 导入成功后写入图片内容指纹；prechecked 为预检返回的 files 列表，用于复用已算哈希。
    // 失败仅告警，不回滚已提交的数据包。
    void indexImportedFiles(qint64 fileId, const QList<ImportEntry> &entries,
                            const QVariantList &prechecked = {});

    LibraryService *m_library;
    StorageService *m_storage;
    TypePackageManager *m_typePackages;
    DedupService *m_dedup;
};
