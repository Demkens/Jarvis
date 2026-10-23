#include "DedupBackfillWorker.h"

#include <QDebug>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <utility>

#include "common/AppError.h"
#include "dedup/ContentHasher.h"
#include "dedup/ImageHasher.h"

DedupBackfillWorker::DedupBackfillWorker(QVector<DedupBackfillItem> items, QObject *parent)
    : QObject(parent)
    , m_items(std::move(items))
{
}

void DedupBackfillWorker::requestCancel()
{
    m_canceled.store(true);
}

void DedupBackfillWorker::run()
{
    const int total = m_items.size();
    int done = 0;
    int failed = 0;
    QVariantList updates;

    const int progressStep = qMax(1, total / 200);
    emit progress(0, total);

    for (int index = 0; index < m_items.size(); ++index) {
        if (m_canceled.load())
            break;

        const DedupBackfillItem &item = m_items.at(index);
        const QFileInfo info(item.absolutePath);
        if (!info.isFile()) {
            ++failed;
            qWarning().noquote() << "[dedup] 补指纹跳过（文件不存在）：" << item.absolutePath;
            ++done;
            if (done % progressStep == 0 || done == total)
                emit progress(done, total);
            continue;
        }

        QVariantMap update{
            {QStringLiteral("fileId"), item.fileId},
            {QStringLiteral("relativePath"), item.relativePath},
            {QStringLiteral("size"), item.size > 0 ? item.size : info.size()},
        };

        bool itemOk = true;

        if (item.needContent) {
            AppError error;
            const QString hash = ContentHasher::sha256File(item.absolutePath, nullptr, &error);
            if (hash.isEmpty()) {
                itemOk = false;
                qWarning().noquote() << "[dedup] 补内容哈希失败：" << item.absolutePath
                                     << error.message;
            } else {
                update.insert(QStringLiteral("contentHash"), hash);
            }
        }

        if (item.needPerceptual) {
            AppError error;
            const ImageFingerprint fingerprint = ImageHasher::compute(item.absolutePath, &error);
            if (!fingerprint.isValid()) {
                itemOk = false;
                qWarning().noquote() << "[dedup] 补感知指纹失败：" << item.absolutePath
                                     << error.message;
            } else {
                update.insert(QStringLiteral("ahash"), fingerprint.ahash);
                update.insert(QStringLiteral("dhash"), fingerprint.dhash);
                update.insert(QStringLiteral("phash"), fingerprint.phash);
            }
        }

        if (!itemOk)
            ++failed;

        // 全部计算失败：不写行，避免下一次补指纹任务反复命中同一批坏文件
        if (!update.contains(QStringLiteral("contentHash"))
            && !update.contains(QStringLiteral("phash"))) {
            ++done;
            if (done % progressStep == 0 || done == total)
                emit progress(done, total);
            continue;
        }

        updates.append(update);
        ++done;
        if (done % progressStep == 0 || done == total)
            emit progress(done, total);
    }

    qInfo().noquote() << "[dedup] 补指纹计算完成：共" << total << "成功" << updates.size()
                      << "失败" << failed << "取消" << m_canceled.load();

    emit finished(
        QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("canceled"), m_canceled.load()},
            {QStringLiteral("total"), total},
            {QStringLiteral("done"), done},
            {QStringLiteral("failed"), failed},
            {QStringLiteral("updates"), updates.size()},
        },
        updates);
}
