// 媒体类型判定：
// - 按扩展名区分核心当前支持的图片/视频格式；
// - 导入扫描、类型契约校验、查重预检与 QML 文件过滤器共用同一份后缀清单，
//   避免多处维护导致口径漂移。
// 仅做文件名判定，不读取文件内容。

#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace MediaTypes {

// 当前支持的图片扩展名（小写、不含点）
inline QStringList imageSuffixes()
{
    return  {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
             QStringLiteral("gif"), QStringLiteral("bmp"),  QStringLiteral("webp")};
}

// 当前支持的视频扩展名（小写、不含点）。
// 只判定扩展名、不解码文件，能否播放最终取决于系统/FFmpeg 后端。
inline QStringList videoSuffixes()
{
    return  {QStringLiteral("mp4"),  QStringLiteral("mkv"),  QStringLiteral("avi"),
             QStringLiteral("mov"),  QStringLiteral("webm"), QStringLiteral("m4v"),
             QStringLiteral("wmv"),  QStringLiteral("flv"),  QStringLiteral("mpeg"),
             QStringLiteral("mpg"),  QStringLiteral("ts")};
}

// 判断文件是否为图片：
// 输入 fileName 可为完整路径或文件名；
// 输出 true 表示扩展名属于当前支持的图片格式。
inline bool isImageFile(const QString &fileName)
{
    return imageSuffixes().contains(QFileInfo(fileName).suffix().toLower());
}

// 判断文件是否为视频：
// 输入 fileName 可为完整路径或文件名；
// 输出 true 表示扩展名属于当前支持的视频格式。
inline bool isVideoFile(const QString &fileName)
{
    return videoSuffixes().contains(QFileInfo(fileName).suffix().toLower());
}

// 判定文件的媒体类别（包内文件清单与导入校验共用）：
// 输入 fileName 可为完整路径或文件名；
// 输出 "image" / "video"；扩展名不属于两者时返回空串。
inline QString mediaKind(const QString &fileName)
{
    if (isImageFile(fileName))
        return QStringLiteral("image");
    if (isVideoFile(fileName))
        return QStringLiteral("video");
    return QString();
}

} // namespace MediaTypes
