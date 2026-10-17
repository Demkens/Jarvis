#pragma once

#include <QString>

class QJsonObject;

// 类型包：声明式资源包（type.json + wizard.qml + viewer.qml）。
// 本类只承载 type.json 契约的解析与自校验结果，不触碰数据库与界面；
// 查看器加载由 TypeEngine 实现。
// 注：类型不再有 DB 侧私有字段，故不再携带 migrate.sql，也不携带分表字段定义。
class TypePackage
{
public:
    QString form;                  // 类型标识（小写字母开头的安全标识符），须与目录名一致
    QString displayName;           // 显示名（如 画作）
    int version = 0;               // 类型包契约版本
    QString coreMinVersion;        // 依赖的最低核心版本
    QString packageLayoutKind;     // 包内文件规范（draw = imageSequence）
    QString browseOrder;           // 预设浏览顺序声明（draw = sequence）

    // 包内资源文件绝对路径（发现阶段由 TypePackageManager 填充）
    QString dirPath;
    QString wizardPath;
    QString viewerPath;

    // 解析并校验 type.json；失败返回 false 并以 error 说明原因
    static bool fromJson(const QJsonObject &json, TypePackage &out, QString &error);
};
