#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>

class QJsonObject;

// 类型包：声明式资源包（type.json + migrate.sql + wizard.qml + viewer.qml）。
// 本类只承载 type.json 契约的解析与自校验结果，不触碰数据库与界面；
// 动态建表 / 通用 CRUD / 查看器加载由 TypeEngine（M3）实现。
class TypePackage
{
public:
    // 分表字段定义；type 取值受控（见 isSupportedFieldType）
    struct Field {
        QString name;
        QString type;
        QString label;             // 表单显示标签
        bool required = false;
        QJsonValue defaultValue;
    };

    QString form;                  // 类型标识 / 分表名（小写字母开头的安全标识符）
    QString displayName;           // 显示名（如 画作）
    int version = 0;               // 类型包契约版本
    QString coreMinVersion;        // 依赖的最低核心版本
    QList<Field> fields;
    QString packageLayoutKind;     // 包内文件规范（draw = imageSequence）
    QString browseOrder;           // 预设浏览顺序声明（draw = sequence）

    // 包内资源文件绝对路径（发现阶段由 TypePackageManager 填充）
    QString dirPath;
    QString migratePath;
    QString wizardPath;
    QString viewerPath;

    // 解析并校验 type.json；失败返回 false 并以 error 说明原因
    static bool fromJson(const QJsonObject &json, TypePackage &out, QString &error);

    // 分表字段的受控类型集（一期）
    static bool isSupportedFieldType(const QString &type);
};
