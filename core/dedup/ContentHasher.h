// 内容指纹计算器（纯函数）：
// - 只做字节级 SHA-256，不访问数据库、不感知 QML、不决定查重范围；
// - 采用分块流式读取，避免把大文件整体载入内存。

#pragma once

#include <QString>

class AppError;

class ContentHasher
{
public:
    // 流式计算文件 SHA-256。
    // 输入：path 为文件绝对路径；sizeBytes 可传 nullptr。
    // 输出：成功返回 64 位小写十六进制字符串，并通过 sizeBytes 写出文件字节数；
    //       失败返回空字符串，错误写入 error。
    static QString sha256File(const QString &path, qint64 *sizeBytes = nullptr,
                              AppError *error = nullptr);
};
