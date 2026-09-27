#pragma once

#include <QString>
#include <QStringList>

#include "common/AppError.h"

class AppPaths;
class CreatorService;
class EventBus;
class LibraryService;

// 导入计划（开发文档 5.4）：向导与预处理两通道最终都归一为此结构，
// 由 StorageService 独占执行——类型包永远不直接操作实际库文件系统。
struct ImportPlan
{
    QString form;            // 目标类型 form（分表名）
    QString title;           // 标题；空则落库为 'untitled'
    QString creatorName;     // 创作者名；空 → anonymous(0)，经 CreatorService find-or-create
    QString creationDate;    // 创作日期 TEXT：YYYY / YYYY-MM / YYYY-MM-DD；空 → NULL
    QStringList files;       // 有序源文件绝对路径，顺序即包内序号（001.ext…）
    QString coverSourcePath; // 封面来源文件；空 → 取 files 首个
};

// 存储服务（开发文档 3.4 / 5.1–5.4）：桶号计算、路径解析、导入执行器、封面生成。
// 存储纪律：
// - 实际库根下为 数字桶/<包id>/，封面在 envs/<库名>/cover/<桶>/<id>.jpg；
// - 桶号由 id 推导：bucket = floor((id-1)/5000) + 1，删除留下的空洞不回收；
// - 导入为复制，绝不移动/删除源文件；
// - 失败回滚：先复制后登记，db 失败 → 回滚事务并删除本次新建的包文件夹与封面。
class StorageService
{
public:
    StorageService(AppPaths *paths, LibraryService *library, CreatorService *creators,
                   EventBus *events);

    // ---- 纯函数约定 ----
    // 桶号推导（开发文档 5.1）
    static int bucketOf(qint64 id);
    // 包内序号文件名的数字部分：1→"001"，999→"999"，1000→"1000"
    static QString indexedName(int index);
    // 分表名只允许安全标识符（会被拼进 SQL）
    static bool isSafeFormName(const QString &form);

    // 创作日期：YYYY / YYYY-MM / YYYY-MM-DD（开发文档 6.1），空串也算合法（未知 → NULL）
    static bool isValidCreationDate(const QString &date);

    // 相对片段 → 绝对路径（开发文档 5.6：db 只存相对片段，路径由核心拼接）
    static QString packageDir(const QString &linkAddress, int bucket, qint64 id);
    static QString coverPath(const QString &coverRoot, int bucket, qint64 id);

    // ---- 封面（开发文档 5.3：核心统一生成 JPEG，长边默认 400px 可调）----
    int coverLongEdge() const { return m_coverLongEdge; }
    void setCoverLongEdge(int px) { m_coverLongEdge = qBound(64, px, 2048); }

    // 导入执行器（开发文档 5.4 ⑥）：
    // 校验 → 开事务 → 解析创作者 → 插入占位行取 id → 复制入桶 → 生成封面
    // → 写分表 → 回填 size/桶号 → 提交 → 广播 PackageCreated。
    // 任一步失败：回滚并删除本次新建的包文件夹与封面，返回 -1。
    qint64 importPackage(const ImportPlan &plan, AppError *error = nullptr);

    // 导出（开发文档 7.4）：把包内文件原样复制到 destDir（保持 001.ext 序号名）。
    // destDir 必须存在且为空（防误覆盖），返回复制的文件数。
    int exportPackage(qint64 id, const QString &destDir, AppError *error = nullptr);

    // 当前库的实际库是否在线（链接库根目录可达；离线时禁止导入/导出）
    bool isLinkOnline() const;

    // 重新生成某包封面（M4 右键"重新生成封面"）：
    // 取 linkAddress/<桶>/<id>/ 内第一个图片文件为源，重写 envs 封面；
    // 包目录缺失/无图片/生成失败均置 error 并返回 false。
    bool regenerateCover(const QString &linkAddress, int bucket, qint64 id, AppError *error);

private:
    bool generateCover(const QString &sourcePath, const QString &targetPath, AppError *error);

    AppPaths *m_paths;
    LibraryService *m_library;
    CreatorService *m_creators;
    EventBus *m_events;
    int m_coverLongEdge = 400;
};
