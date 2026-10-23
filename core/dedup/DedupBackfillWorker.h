// 补指纹工作线程对象：
// - 接收「需要补算的行」清单，在独立 QThread 中重算 content_hash / 感知指纹；
// - 不访问数据库：数据库写入由 DedupService 在主线程统一执行（插入或按列覆盖）。

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>

// 一条待补指纹的文件记录。
struct DedupBackfillItem
{
    qint64 fileId = 0;       // 所属数据包 id
    QString relativePath;    // 包内相对路径（也是 photohash 主键的一部分）
    QString absolutePath;    // 实际文件绝对路径
    qint64 size = 0;         // 已知字节数；<=0 时由工作线程读取
    bool needContent = false;    // 需要补 content_hash
    bool needPerceptual = false; // 需要补 aHash/dHash/pHash
};

class DedupBackfillWorker : public QObject
{
    Q_OBJECT

public:
    // 输入 items 为待补清单；对象接管清单所有权（按值保存）。
    explicit DedupBackfillWorker(QVector<DedupBackfillItem> items, QObject *parent = nullptr);

    // 请求取消：线程安全，可从主线程直接调用。
    void requestCancel();

public slots:
    // 执行补算。结束时发出 finished（汇总 + 待写入数据库的行）。
    void run();

signals:
    // 进度：done/total（按文件计）。
    void progress(int done, int total);
    // 结束：summary 含 total/done/failed/canceled；updates 为可写入 photohash 的行。
    void finished(const QVariantMap &summary, const QVariantList &updates);

private:
    QVector<DedupBackfillItem> m_items;
    std::atomic_bool m_canceled{false};
};
