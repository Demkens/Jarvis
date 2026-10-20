#include "TypePackage.h"

#include <QDebug>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

// form 会成为 SQL 标识符与目录名，限定小写字母开头的安全命名
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
    out.packageLayoutKind = layoutKind;
    out.browseOrder = browseOrder;

    // stats：本期只解析不渲染。字段缺失即空；存在但不是 JSON 对象时仅告警，
    // 不影响类型包加载（扩展点不应成为加载门槛）
    const QJsonValue statsValue = json.value(QStringLiteral("stats"));
    out.stats = statsValue.isObject() ? statsValue.toObject().toVariantMap() : QVariantMap{};
    if (!statsValue.isObject() && !statsValue.isUndefined() && !statsValue.isNull())
        qWarning().noquote() << "[types]" << form << "stats 声明不是 JSON 对象，已忽略";
    return true;
}
