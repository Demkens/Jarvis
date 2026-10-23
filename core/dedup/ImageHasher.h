// 图片感知指纹计算器（纯函数）：
// - 只负责解码图片并计算 aHash / dHash / pHash，不访问数据库、不感知 QML、不决定查重口径；
// - 解码时按 EXIF 方向自动转正、透明通道合成到白底，并缩到小尺寸后再计算，控制内存与耗时；
// - 三种指纹统一以 16 位十六进制文本保存：aHash/dHash 为完整 64 位，
//   pHash 按标准口径只取 63 位有效信息（跳过直流分量，最高位固定 0）。

#pragma once

#include <QString>

class AppError;

// 一张图片的三项感知指纹（均为 16 位十六进制文本；计算失败时为空）。
struct ImageFingerprint
{
    QString ahash; // 均值哈希：8x8 灰度与均值比较
    QString dhash; // 差值哈希：9x8 灰度水平相邻比较
    QString phash; // 感知哈希：32x32 灰度 DCT 的 8x8 低频系数（跳过 DC）与中位数比较

    // 三项指纹是否齐全。
    bool isValid() const { return !ahash.isEmpty() && !dhash.isEmpty() && !phash.isEmpty(); }
};

// 图片指纹与距离工具（无状态）。
class ImageHasher
{
public:
    // 计算单张图片的感知指纹。
    // 输入：path 图片绝对路径；error 可空，失败时写入错误码与消息。
    // 输出：成功返回三项指纹；解码失败返回空结构。
    static ImageFingerprint compute(const QString &path, AppError *error = nullptr);

    // 计算两个 64 位指纹的汉明距离。
    // 输入：aHex / bHex 为 compute 生成的 16 位十六进制文本。
    // 输出：0–64；任一文本非法（非十六进制或长度不符）返回 -1。
    static int hamming(const QString &aHex, const QString &bHex);
};
