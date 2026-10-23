// 查重扫描工作线程对象：
// - 持有扫描所需的纯数据（库内指纹行 + 外部文件夹路径 + 阈值），在独立 QThread 中执行；
// - 不访问数据库（QSqlDatabase 连接绑定主线程），库内数据由 DedupService 在主线程取好后传入；
// - 外部文件的 content_hash 与感知指纹只存在于本对象内存中，任务结束/取消即随对象释放。
// 职责边界：只做「枚举外部图片 → 计算指纹 → 精确/近似分组 → 生成结果行」，不写库、不感知 QML。

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>

// 扫描候选项：库内文件（来自 photohash 行）或外部文件（扫描文件夹得到）。
struct DedupScanItem
{
    qint64 fileId = 0;      // 库内包 id；外部文件为 0
    QString relativePath;   // 库内包内相对路径；外部文件为空
    QString absolutePath;   // 实际绝对路径
    QString title;          // 库内包标题；外部为文件名
    QString typeName;       // 库内类型显示名
    QString creatorName;    // 库内主要创作者
    qint64 size = 0;        // 文件字节数
    QString contentHash;    // SHA-256（外部文件现场计算）
    QString ahash;          // 感知指纹（外部文件现场计算）
    QString dhash;
    QString phash;
    bool external = false;  // true = 外部文件
};

class DedupScanWorker : public QObject
{
    Q_OBJECT

public:
    // 输入：items 库内候选项（绝对值路径已由服务层拼好）；externalFolder 为空表示不扫外部；
    //       recursive 控制是否递归子文件夹；threshold=0 为精确查重，1–16 为 pHash 汉明距离上限。
    DedupScanWorker(QVector<DedupScanItem> items, const QString &externalFolder, bool recursive,
                    int threshold, QObject *parent = nullptr);

    // 请求取消：线程安全，可从主线程直接调用；工作线程在每个文件/每批比较前检查。
    void requestCancel();

public slots:
    // 执行扫描（由 QThread::started 触发）。结束时发出 finished（含汇总与结果行）。
    void run();

signals:
    // 阶段进度：phase 为 "prepare"（外部文件指纹预处理）或 "compare"（疑似文件比较）。
    // done/total 供进度条展示；total<=0 表示总数未知。
    void progress(const QString &phase, int done, int total);
    // 扫描结束：summary 为统计信息（含 canceled/groups 等），rows 为结果行（仅重复/近似文件）。
    void finished(const QVariantMap &summary, const QVariantList &rows);

private:
    QVector<DedupScanItem> m_items;
    QString m_externalFolder;
    bool m_recursive = true;
    int m_threshold = 0;
    std::atomic_bool m_canceled{false};
};
