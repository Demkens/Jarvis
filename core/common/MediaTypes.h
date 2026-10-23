// 媒体类型判定：
// - 按扩展名区分核心当前支持的图片格式；
// - 导入扫描与查重预检共用同一份后缀清单，避免两处维护导致口径漂移。
// 仅做文件名判定，不读取文件内容。

#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace MediaTypes {

// 判断文件是否为图片：
// 输入 fileName 可为完整路径或文件名；
// 输出 true 表示扩展名属于当前支持的图片格式。
inline bool isImageFile(const QString &fileName)
{
    static const QStringList kImageSuffixes = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("gif"), QStringLiteral("bmp"),  QStringLiteral("webp"),
    };
    return kImageSuffixes.contains(QFileInfo(fileName).suffix().toLower());
}

} // namespace MediaTypes
