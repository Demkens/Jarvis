#include "TypePackage.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

// form 会成为 SQL 标识符与目录名，限定小写字母开头的安全命名
const QRegularExpression kIdentifierPattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
const QRegularExpression kVersionPattern(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));

// 解析顶层 media 声明：
// 输入 mediaValue 为 type.json 的 media 字段，layoutKind 为包内布局；
// 输出 media（按声明顺序去重）或 error。
// 规则：singleMedia / mediaSequence 必须显式声明；imageSequence 不得声明 video；
//       wholeBookText 缺省为空、声明了则按声明采用（核心不消费文本媒体）。
bool parseMedia(const QJsonValue &mediaValue, const QString &layoutKind, QStringList &media,
                QString &error)
{
    const bool mediaRequired = layoutKind == QStringLiteral("singleMedia")
        || layoutKind == QStringLiteral("mediaSequence");

    if (mediaValue.isUndefined() || mediaValue.isNull()) {
        if (mediaRequired) {
            error = QStringLiteral("packageLayout %1 必须显式声明 media").arg(layoutKind);
            return false;
        }
        if (layoutKind == QStringLiteral("imageSequence"))
            media = {QStringLiteral("image")}; // 历史类型包未声明 media，按图片序列推导
        return true;
    }

    if (!mediaValue.isArray()) {
        error = QStringLiteral("media 须为字符串数组（如 [\"video\"]）");
        return false;
    }

    for (const QJsonValue &value : mediaValue.toArray()) {
        const QString kind = value.toString().trimmed().toLower();
        if (kind != QStringLiteral("image") && kind != QStringLiteral("video")) {
            error = QStringLiteral("media 仅支持 \"image\" 或 \"video\"，非法值：") + kind;
            return false;
        }
        if (media.contains(kind)) {
            error = QStringLiteral("media 重复声明：") + kind;
            return false;
        }
        media.append(kind);
    }

    if (media.isEmpty()) {
        error = QStringLiteral("media 为空数组；无需媒体时请省略该字段");
        return false;
    }
    if (layoutKind == QStringLiteral("imageSequence") && media.contains(QStringLiteral("video"))) {
        error = QStringLiteral("imageSequence 布局只能处理图片，不能声明 video");
        return false;
    }
    return true;
}

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

    // —— 媒体声明 ——
    QStringList media;
    if (!parseMedia(json.value(QStringLiteral("media")), layoutKind, media, error))
        return false;

    // —— 校验通过，落盘 ——
    out.form = form;
    out.displayName = displayName;
    out.version = static_cast<int>(version);
    out.coreMinVersion = coreMinVersion;
    out.packageLayoutKind = layoutKind;
    out.media = media;
    out.packageLayout = layoutValue.toObject().toVariantMap();
    out.browseOrder = browseOrder;

    // stats：本期只解析不渲染。字段缺失即空；存在但不是 JSON 对象时仅告警，
    // 不影响类型包加载（扩展点不应成为加载门槛）
    const QJsonValue statsValue = json.value(QStringLiteral("stats"));
    out.stats = statsValue.isObject() ? statsValue.toObject().toVariantMap() : QVariantMap{};
    if (!statsValue.isObject() && !statsValue.isUndefined() && !statsValue.isNull())
        qWarning().noquote() << "[types]" << form << "stats 声明不是 JSON 对象，已忽略";
    return true;
}
