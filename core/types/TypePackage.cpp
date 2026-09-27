#include "TypePackage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

// form 与字段名都会成为 SQL 标识符，限定小写字母开头的安全命名
const QRegularExpression kIdentifierPattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
const QRegularExpression kVersionPattern(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));

bool checkIdentifier(const QString &value, const QString &what, QString &error)
{
    if (!kIdentifierPattern.match(value).hasMatch()) {
        error = what + QStringLiteral(" 命名非法（须为小写字母开头的 SQL 安全标识符）：") + value;
        return false;
    }
    return true;
}

} // namespace

bool TypePackage::isSupportedFieldType(const QString &type)
{
    static const QSet<QString> kSupported = {
        QStringLiteral("int"),
        QStringLiteral("text"),
        QStringLiteral("date"),     // YYYY / YYYY-MM / YYYY-MM-DD，可空
        QStringLiteral("creator"),  // 创作者引用，经核心 find-or-create
    };
    return kSupported.contains(type);
}

bool TypePackage::fromJson(const QJsonObject &json, TypePackage &out, QString &error)
{
    // —— 元数据 ——
    const QString form = json.value(QStringLiteral("form")).toString();
    if (!checkIdentifier(form, QStringLiteral("form"), error))
        return false;

    const QString displayName = json.value(QStringLiteral("displayName")).toString();
    if (displayName.isEmpty()) {
        error = QStringLiteral("displayName 缺失或为空");
        return false;
    }

    const QJsonValue versionValue = json.value(QStringLiteral("version"));
    const double version = versionValue.toDouble(-1.0);
    if (!versionValue.isDouble() || version < 1.0 || version != qFloor(version)) {
        error = QStringLiteral("version 须为 >= 1 的整数");
        return false;
    }

    const QString coreMinVersion = json.value(QStringLiteral("coreMinVersion")).toString();
    if (!kVersionPattern.match(coreMinVersion).hasMatch()) {
        error = QStringLiteral("coreMinVersion 缺失或格式非法（须为 x.y.z）：") + coreMinVersion;
        return false;
    }

    // —— 分表字段 ——
    const QJsonValue fieldsValue = json.value(QStringLiteral("fields"));
    if (!fieldsValue.isArray()) {
        error = QStringLiteral("fields 缺失或不是数组");
        return false;
    }

    QList<Field> fields;
    QSet<QString> fieldNames;
    const QJsonArray fieldArray = fieldsValue.toArray();
    for (const QJsonValue &item : fieldArray) {
        if (!item.isObject()) {
            error = QStringLiteral("fields 数组元素须为对象");
            return false;
        }
        const QJsonObject fieldObj = item.toObject();

        Field field;
        field.name = fieldObj.value(QStringLiteral("name")).toString();
        if (!checkIdentifier(field.name, QStringLiteral("字段名"), error))
            return false;
        if (fieldNames.contains(field.name)) {
            error = QStringLiteral("字段名重复：") + field.name;
            return false;
        }
        fieldNames.insert(field.name);

        field.type = fieldObj.value(QStringLiteral("type")).toString();
        if (!isSupportedFieldType(field.type)) {
            error = QStringLiteral("字段 %1 的类型不受支持：%2").arg(field.name, field.type);
            return false;
        }

        field.label = fieldObj.value(QStringLiteral("label")).toString();
        if (field.label.isEmpty()) {
            error = QStringLiteral("字段 %1 缺少显示标签 label").arg(field.name);
            return false;
        }

        field.required = fieldObj.value(QStringLiteral("required")).toBool(false);
        field.defaultValue = fieldObj.value(QStringLiteral("default"));
        fields.append(field);
    }

    // —— 包内规范与浏览顺序 ——
    const QJsonValue layoutValue = json.value(QStringLiteral("packageLayout"));
    const QString layoutKind = layoutValue.toObject().value(QStringLiteral("kind")).toString();
    if (!layoutValue.isObject() || layoutKind.isEmpty()) {
        error = QStringLiteral("packageLayout 缺失或缺少 kind");
        return false;
    }

    const QString browseOrder = json.value(QStringLiteral("browseOrder")).toString();
    if (browseOrder.isEmpty()) {
        error = QStringLiteral("browseOrder 缺失或为空");
        return false;
    }

    // —— 校验通过，落盘 ——
    out.form = form;
    out.displayName = displayName;
    out.version = static_cast<int>(version);
    out.coreMinVersion = coreMinVersion;
    out.fields = fields;
    out.packageLayoutKind = layoutKind;
    out.browseOrder = browseOrder;
    return true;
}
