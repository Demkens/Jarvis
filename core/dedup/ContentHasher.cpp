#include "ContentHasher.h"

#include <QCryptographicHash>
#include <QFile>

#include "common/AppError.h"

QString ContentHasher::sha256File(const QString &path, qint64 *sizeBytes, AppError *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = AppError::fail(
                AppError::Io,
                QStringLiteral("无法读取文件（可能被占用或权限不足）：%1").arg(path));
        }
        return {};
    }

    // 读取前取大小；文件在读取期间被改写属于极端并发场景，不在此处理
    if (sizeBytes != nullptr)
        *sizeBytes = file.size();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr qint64 kChunkSize = 1024 * 1024;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (file.error() != QFileDevice::NoError) {
                if (error != nullptr) {
                    *error = AppError::fail(
                        AppError::Io,
                        QStringLiteral("读取文件失败：%1").arg(file.errorString()));
                }
                return {};
            }
            break;
        }
        hash.addData(chunk);
    }

    return QString::fromLatin1(hash.result().toHex());
}
