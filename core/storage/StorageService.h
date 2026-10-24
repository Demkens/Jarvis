#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "common/AppError.h"

class AppPaths;
class CreatorService;
class EventBus;
class LibraryService;

// 包内条目：一个源文件落到包内的哪个相对路径。
// 相对路径由计划产出方决定（draw = "001.jpg"，novel = "12.txt"），
// 核心只做安全校验与落位，不再假设扁平命名。
struct ImportEntry
{
    QString sourcePath;    // 源文件绝对路径
    QString relativePath;  // 包内相对路径：'/' 分隔，不得为绝对路径、不得含 ".."
};

// 导入计划（开发文档 5.4）：向导与预处理两通道最终都归一为此结构，
// 由 StorageService 独占执行——类型包永远不直接操作实际库文件系统。
struct ImportPlan
{
    QString form;            // 目标类型 form（须与类型包 type.json 的 form 一致）
    QString title;           // 标题；空则落库为 'untitled'
    QString creatorName;     // 创作者名；空 → 「佚名」，经 CreatorService find-or-create
    QString creationDate;    // 创作日期 TEXT：严格 YYYY-MM-DD；空 → 落库取当天兜底
    int score = 0;           // 初始评分 0–100；0 = 未评分
    QList<ImportEntry> entries; // 有序包内条目，顺序即落位顺序
    QString coverSourcePath; // 封面来源文件；空 → 不生成封面（如纯文本包）
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
    // 包内相对路径 → 包目录下绝对路径（查重/补指纹定位实际文件用）
    static QString packageFilePath(const QString &linkAddress, int bucket, qint64 id,
                                   const QString &relativePath);
    // 类型标识合法性校验（安全标识符：小写字母开头）。类型已无分表，form 不再拼进 SQL，
    // 此处仅作外部输入的格式校验。
    static bool isSafeFormName(const QString &form);

    // 创作日期：严格 YYYY-MM-DD（v5 起升格为核心字段 file.creation_date，精度固定、导入必填）
    static bool isValidCreationDate(const QString &date);

    // 相对片段 → 绝对路径（开发文档 5.6：db 只存相对片段，路径由核心拼接）
    static QString packageDir(const QString &linkAddress, int bucket, qint64 id);
    static QString coverPath(const QString &coverRoot, int bucket, qint64 id);

    // ---- 封面（开发文档 5.3：核心统一生成 JPEG，清晰度固定为对角线 512px，不对外可调）----

    // 导入执行器（开发文档 5.4 ⑥）：
    // 校验 → 开事务 → 解析创作者 → 插入占位行取 id → 复制入桶 → 生成封面
    // → 回填 size/桶号 → 提交 → 广播 PackageCreated。
    // 任一步失败：回滚并删除本次新建的包文件夹与封面，返回 -1。
    qint64 importPackage(const ImportPlan &plan, AppError *error = nullptr);

    // 导出（开发文档 7.4）：把包内文件原样复制到 destDir（保持 001.ext 序号名）。
    // destDir 必须存在且为空（防误覆盖），返回复制的文件数。
    int exportPackage(qint64 id, const QString &destDir, AppError *error = nullptr);

    // 当前库的实际库是否在线（链接库根目录可达；离线时禁止导入/导出）
    bool isLinkOnline() const;

    // 补全存量行的封面长宽比（瀑布流布局用）：对 aspect_ratio<=0 的行按封面文件头回写 AR，
    // 封面缺失兜底 1.0。返回更新行数，-1 表示出错。打开库后调用一次即可（无待补行时开销极小）。
    int backfillAspectRatios(AppError *error = nullptr);

private:
    bool generateCover(const QString &sourcePath, const QString &targetPath, AppError *error);

    AppPaths *m_paths;
    LibraryService *m_library;
    CreatorService *m_creators;
    EventBus *m_events;
};
