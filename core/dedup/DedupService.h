// 查重服务（M9）：
// - 字节级 content_hash 用于「完全重复」；感知指纹 aHash/dHash/pHash 用于“近似重复”；
// - 对外是 QML 门面：导入前预检、导入后写指纹、查重扫描、补指纹、取消与会话清理；
// - 耗时的读盘/解码在独立工作线程执行，数据库读写始终留在主线程（连接绑定主线程）；
// - 外部文件指纹只存在于工作线程对象的内存会话里，扫描结束/取消/关窗即释放。
// 纪律：本服务只读写 photohash，不执行导入、不写用户文件、不感知 QML 布局。

#pragma once

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "dedup/DedupBackfillWorker.h"
#include "dedup/DedupScanWorker.h"

class LibraryService;

class DedupService : public QObject
{
    Q_OBJECT

public:
    // 输入 library 为已装配的库服务；本服务不持有其所有权。
    explicit DedupService(LibraryService *library, QObject *parent = nullptr);
    ~DedupService() override;

    // 导入前完全重复预检（文件大小预筛 + content_hash 点查）。
    // 输入 files：源文件绝对路径列表。
    // 输出：
    // { ok, message?,
    //   checked: 参与检查的文件数,
    //   files: [{ path, size, hashed, contentHash, duplicate,
    //             existing: [{ fileId, title, typeName, relativePath }] }],
    //   skipped: [{ path, reason }],
    //   hasDuplicates: 是否存在完全重复 }
    // 仅对「库中存在同尺寸文件」的源文件计算 content_hash；尺寸不匹配的直接判定为非重复。
    Q_INVOKABLE QVariantMap precheck(const QVariantList &files);

    // 导入成功后写入图片内容指纹与感知指纹。
    // 输入 entries：[{ relativePath: 包内 001.ext, sourcePath: 源文件绝对路径,
    //                  contentHash?: 预检已算出的哈希, size?: 预检已取到的文件大小 }]。
    //       contentHash/size 缺省时现场计算，预检命中候选项可直接复用，避免二次读盘。
    // 输出：成功返回写入行数；库未打开、事务失败返回 -1。
    // 单个文件读取失败时仍写入该行（对应指纹为 NULL），便于补指纹流程识别。
    int indexPackage(qint64 fileId, const QVariantList &entries);

    // 指纹覆盖情况（供查重窗口在扫描前提示缺指纹）。
    // 输入 query：{ allTypes: bool, forms: [类型 form] }；缺省视为全部类型。
    // 输出：{ ok, message?, totalRows, missingContent, missingPerceptual, missingAny,
    //         packagesWithoutRows }。
    Q_INVOKABLE QVariantMap fingerprintStatus(const QVariantMap &query);

    // 启动查重扫描（异步）。
    // 输入 query：
    //   allTypes: bool              是否覆盖当前库全部类型
    //   forms: [string]             allTypes=false 时的类型 form 列表
    //   threshold: int              0=精确（content_hash），1–16=近似（pHash 汉明距离）
    //   externalFolder: string      附件文件夹（空=不扫外部）
    //   recursive: bool             附件文件夹是否递归
    // 输出：{ ok, message? }；立即返回，进度/结果经 scanProgress / scanFinished 信号送出。
    Q_INVOKABLE QVariantMap startScan(const QVariantMap &query);

    // 启动补指纹（异步，仅针对当前库内所选类型）。
    // 输入 query 同上（threshold/externalFolder 被忽略）。
    // 输出：{ ok, message?, nothing?: bool }；进度/结果经 backfillProgress / backfillFinished 送出。
    Q_INVOKABLE QVariantMap startBackfill(const QVariantMap &query);

    // 取消当前扫描或补指纹（未运行则无操作）。
    Q_INVOKABLE void cancel();

    // 结束外部文件的指纹内存会话：正在运行的任务会被取消，随后释放。
    // 查重窗口关闭时调用；不影响库内已持久化的指纹。
    Q_INVOKABLE void clearSession();

signals:
    // 扫描阶段进度：phase 为 "prepare"（外部文件预处理）或 "compare"（疑似文件比较）。
    void scanProgress(const QString &phase, int done, int total);
    // 扫描结束：summary 为统计信息，rows 为重复/近似文件结果行（按组排列）。
    void scanFinished(const QVariantMap &summary, const QVariantList &rows);
    // 补指纹进度（按文件计）。
    void backfillProgress(int done, int total);
    // 补指纹结束：summary 含 total/done/failed/written/canceled 等。
    void backfillFinished(const QVariantMap &summary);

private:
    QVariantMap fail(const QString &message) const;

    // 把 query 的类型范围转换为 SQL 片段（空串=全部类型）；forms 以参数绑定。
    static void buildTypeFilter(const QVariantMap &query, QString *clause, QVariantMap *params);

    // 取当前库的扫描候选项（库内 photohash 行 → 绝对路径 + 元数据）。
    // 输出：成功返回 true 并填充 items；失败返回 false 并写 message。
    bool collectScanItems(const QVariantMap &query, QVector<DedupScanItem> *items,
                          QString *message) const;

    // 取待补指纹记录：已有行缺哈希 + 完全没有 photohash 行的旧包（按包内实际图片建行）。
    // 输出：返回可补条目数；warningCount 记录目录缺失等跳过数。
    int collectBackfillItems(const QVariantMap &query, QVector<DedupBackfillItem> *items,
                             int *warningCount) const;

    // 把工作线程算出的指纹写回 photohash（主线程事务）。
    // 输出：成功返回写入行数；失败返回 -1。
    int writeFingerprintUpdates(const QVariantList &updates);

    // 确保上一个工作线程已收尾（避免连续启动时指针悬挂）。
    void waitForPreviousThread();
    // 请求当前工作任务取消（线程安全）。
    void cancelCurrent();

    LibraryService *m_library;
    QPointer<QThread> m_thread;
    QPointer<DedupScanWorker> m_scanWorker;
    QPointer<DedupBackfillWorker> m_backfillWorker;
    bool m_busy = false;
    // 任务代号：每次启动递增，用来丢弃上一任务残留的结束回调
    int m_generation = 0;
};
