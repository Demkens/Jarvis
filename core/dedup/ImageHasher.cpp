#include "ImageHasher.h"

#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "common/AppError.h"

namespace {

// 统一解码尺寸：先整体缩到 64x64（不保留宽高比）再派生各哈希，兼顾细节与耗时
constexpr int kDecodeSize = 64;
// pHash 的 DCT 输入尺寸与保留的低频块尺寸
constexpr int kPHashSize = 32;
constexpr int kPHashLow = 8;
constexpr double kPi = 3.14159265358979323846;

int popCount(quint64 value)
{
    int count = 0;
    while (value != 0) {
        value &= value - 1; // 每次消掉最低位的 1
        ++count;
    }
    return count;
}

QString toHex(quint64 bits)
{
    return QStringLiteral("%1").arg(bits, 16, 16, QLatin1Char('0'));
}

// 解码并归一化为 64x64 灰度图；alpha 先合成到白底，避免透明区域被当作黑色参与比较。
QImage loadNormalizedGray(const QString &path, AppError *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // 按 EXIF 方向转正
    reader.setScaledSize(QSize(kDecodeSize, kDecodeSize));
    QImage image = reader.read();

    if (image.isNull()) {
        // 个别解码器不支持缩放读取：退回原尺寸再缩放
        QImageReader fallback(path);
        fallback.setAutoTransform(true);
        image = fallback.read();
        if (image.isNull()) {
            if (error != nullptr) {
                *error = AppError::fail(
                    AppError::Io,
                    QStringLiteral("图片解码失败：%1（%2）")
                        .arg(QDir::toNativeSeparators(path), fallback.errorString()));
            }
            return {};
        }
    }

    const bool hasAlpha = image.hasAlphaChannel();
    if (image.size() != QSize(kDecodeSize, kDecodeSize)) {
        image = image.scaled(kDecodeSize, kDecodeSize, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (hasAlpha) {
        QImage canvas(image.size(), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        QPainter painter(&canvas);
        painter.drawImage(0, 0, image);
        painter.end();
        image = canvas;
    }

    return image.convertToFormat(QImage::Format_Grayscale8);
}

// 8x8 均值哈希：像素 ≥ 全图均值记 1。
quint64 hashAverage(const QImage &gray)
{
    const QImage small =
        gray.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    double sum = 0;
    for (int y = 0; y < small.height(); ++y) {
        const uchar *line = small.constScanLine(y);
        for (int x = 0; x < small.width(); ++x)
            sum += line[x];
    }
    const double average = sum / (small.width() * small.height());

    quint64 bits = 0;
    for (int y = 0; y < small.height(); ++y) {
        const uchar *line = small.constScanLine(y);
        for (int x = 0; x < small.width(); ++x)
            bits = (bits << 1) | (static_cast<double>(line[x]) >= average ? 1ULL : 0ULL);
    }
    return bits;
}

// 9x8 差值哈希：每行右侧像素 > 左侧像素记 1。
quint64 hashDifference(const QImage &gray)
{
    const QImage small =
        gray.scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    quint64 bits = 0;
    for (int y = 0; y < small.height(); ++y) {
        const uchar *line = small.constScanLine(y);
        for (int x = 0; x < small.width() - 1; ++x)
            bits = (bits << 1) | (line[x + 1] > line[x] ? 1ULL : 0ULL);
    }
    return bits;
}

// 32x32 灰度做二维 DCT-II（行列分离，避免 O(N^4)），返回按 [v][u] 排列的系数。
QVector<double> dct2d(const QVector<double> &input, int size)
{
    QVector<double> cosine(size * size);
    for (int x = 0; x < size; ++x) {
        for (int u = 0; u < size; ++u)
            cosine[x * size + u] = std::cos((2 * x + 1) * u * kPi / (2.0 * size));
    }

    // 先对每行做一维 DCT
    QVector<double> rows(size * size);
    for (int y = 0; y < size; ++y) {
        for (int u = 0; u < size; ++u) {
            double sum = 0;
            for (int x = 0; x < size; ++x)
                sum += input[y * size + x] * cosine[x * size + u];
            rows[y * size + u] = sum;
        }
    }

    // 再对每列做一维 DCT
    QVector<double> coefficients(size * size);
    for (int x = 0; x < size; ++x) {
        for (int v = 0; v < size; ++v) {
            double sum = 0;
            for (int y = 0; y < size; ++y)
                sum += rows[y * size + x] * cosine[y * size + v];
            coefficients[v * size + x] = sum;
        }
    }
    return coefficients;
}

// pHash：取 8x8 低频系数与其中位数比较，得到 64 位。
// 说明：直流分量也参与，用于补足 64 位；中位数以全部 64 个系数计算，亮度变化会被中位数吸收。
quint64 hashPerceptual(const QImage &gray)
{
    const QImage small =
        gray.scaled(kPHashSize, kPHashSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QVector<double> pixels(kPHashSize * kPHashSize);
    for (int y = 0; y < kPHashSize; ++y) {
        const uchar *line = small.constScanLine(y);
        for (int x = 0; x < kPHashSize; ++x)
            pixels[y * kPHashSize + x] = line[x];
    }

    const QVector<double> coefficients = dct2d(pixels, kPHashSize);

    QVector<double> low;
    low.reserve(kPHashLow * kPHashLow);
    for (int v = 0; v < kPHashLow; ++v) {
        for (int u = 0; u < kPHashLow; ++u)
            low.append(coefficients[v * kPHashSize + u]);
    }

    QVector<double> sorted = low;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double median = sorted.at(sorted.size() / 2);

    quint64 bits = 0;
    for (double value : low)
        bits = (bits << 1) | (value > median ? 1ULL : 0ULL);
    return bits;
}

} // namespace

ImageFingerprint ImageHasher::compute(const QString &path, AppError *error)
{
    ImageFingerprint fingerprint;
    if (path.isEmpty()) {
        if (error != nullptr)
            *error = AppError::fail(AppError::Validation, QStringLiteral("图片路径为空"));
        return fingerprint;
    }

    const QImage gray = loadNormalizedGray(path, error);
    if (gray.isNull())
        return fingerprint;

    fingerprint.ahash = toHex(hashAverage(gray));
    fingerprint.dhash = toHex(hashDifference(gray));
    fingerprint.phash = toHex(hashPerceptual(gray));
    return fingerprint;
}

int ImageHasher::hamming(const QString &aHex, const QString &bHex)
{
    bool aOk = false;
    bool bOk = false;
    const quint64 a = aHex.toULongLong(&aOk, 16);
    const quint64 b = bHex.toULongLong(&bOk, 16);
    if (!aOk || !bOk || aHex.size() != 16 || bHex.size() != 16)
        return -1;
    return popCount(a ^ b);
}
