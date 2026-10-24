#include "BookSplitter.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextCodec>

namespace {

// 标题行长度上限：正文里的「第一章」式叙述通常很长，用长度排除误判
constexpr int kMaxHeadingLength = 60;
// 解析规则表时接受的两级标记
const QString kLevelChapter = QStringLiteral("chapter");
const QString kLevelVolume = QStringLiteral("volume");
// 首个章节标题之前的内容归入该伪章，避免丢字
const QString kPrefaceTitle = QStringLiteral("开篇");
// 包清单文件名（沿用开发文档 6.6 的命名）
const QString kManifestName = QStringLiteral("config.txt");

struct Rule
{
    QString id;
    QString level;
    QRegularExpression pattern;
};

// 一行是否为「候选标题」（非空且不过长）
bool isHeadingCandidate(const QString &line)
{
    const QString trimmed = line.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= kMaxHeadingLength;
}

// 统一换行后按行拆分；末尾的连续空行去掉，避免章节文件尾部堆积空行
QStringList splitLines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = normalized.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    return lines;
}

// 规则表 → Rule 列表；非法正则与未知 level 记入 warnings 后跳过
QList<Rule> parseRules(const QVariantList &rules, QStringList &warnings)
{
    QList<Rule> parsed;
    for (const QVariant &value : rules) {
        const QVariantMap map = value.toMap();
        Rule rule;
        rule.id = map.value(QStringLiteral("id")).toString();
        rule.level = map.value(QStringLiteral("level")).toString();
        const QString pattern = map.value(QStringLiteral("pattern")).toString();
        if (rule.id.isEmpty() || rule.level.isEmpty() || pattern.isEmpty()) {
            warnings.append(QStringLiteral("chapterRules 存在缺字段的规则，已跳过"));
            continue;
        }
        if (rule.level != kLevelChapter && rule.level != kLevelVolume) {
            warnings.append(QStringLiteral("chapterRules 规则 %1 的 level 非法，已跳过").arg(rule.id));
            continue;
        }
        rule.pattern = QRegularExpression(pattern);
        if (!rule.pattern.isValid()) {
            warnings.append(QStringLiteral("chapterRules 规则 %1 的正则非法，已跳过").arg(rule.id));
            continue;
        }
        parsed.append(rule);
    }
    return parsed;
}

// 行区间 → 章节正文（去掉尾部空行，非空时补一个换行）
QString sliceLines(const QStringList &lines, int start, int end, bool *hasContent)
{
    QStringList slice = lines.mid(start, end - start);
    while (!slice.isEmpty() && slice.last().trimmed().isEmpty())
        slice.removeLast();
    if (hasContent != nullptr)
        *hasContent = !slice.isEmpty();
    if (slice.isEmpty())
        return QString();
    return slice.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// 章节标记（行号 + 类型 + 标题），用于一次性构建卷章结构
struct Marker
{
    int line = 0;
    bool volume = false;
    QString title;
};

} // namespace

QString BookSplitter::decodeText(const QByteArray &bytes, QString *encoding)
{
    QByteArray data = bytes;
    // 去掉 UTF-8 BOM，避免首行匹配「第一章」时被前导字节干扰
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);

    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    if (!utf8.hasError()) {
        if (encoding != nullptr)
            *encoding = QStringLiteral("UTF-8");
        return text;
    }

    // 中文小说 txt 的历史遗留编码以 GB18030 为主（GBK 的超集）
    if (QTextCodec *codec = QTextCodec::codecForName("GB18030")) {
        if (encoding != nullptr)
            *encoding = QStringLiteral("GB18030");
        return codec->toUnicode(bytes);
    }

    if (encoding != nullptr)
        *encoding = QStringLiteral("未知");
    return QString();
}

BookAnalysis BookSplitter::analyze(const QString &path, const QVariantList &rules)
{
    BookAnalysis result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("源文件无法读取：%1").arg(QDir::toNativeSeparators(path));
        return result;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.trimmed().isEmpty()) {
        result.error = QStringLiteral("源文件为空：%1").arg(QDir::toNativeSeparators(path));
        return result;
    }

    QString encoding;
    const QString text = decodeText(bytes, &encoding);
    if (text.isEmpty()) {
        result.error = QStringLiteral("文本解码失败（UTF-8 与 GB18030 均不可读）");
        return result;
    }
    result.encoding = encoding;
    if (encoding != QStringLiteral("UTF-8"))
        result.warnings.append(QStringLiteral("源文件不是 UTF-8，已按 %1 解码").arg(encoding));

    QStringList ruleWarnings;
    const QList<Rule> parsedRules = parseRules(rules, ruleWarnings);
    result.warnings.append(ruleWarnings);

    const QStringList lines = splitLines(text);

    // 1. 选章节规则：命中行数最多者胜（并列取规则表中靠前者）
    const Rule *bestChapterRule = nullptr;
    int bestHits = 0;
    for (const Rule &rule : parsedRules) {
        if (rule.level != kLevelChapter)
            continue;
        int hits = 0;
        for (const QString &line : lines) {
            if (isHeadingCandidate(line) && rule.pattern.match(line).hasMatch())
                ++hits;
        }
        if (hits > bestHits) {
            bestHits = hits;
            bestChapterRule = &rule;
        }
    }

    // 2. 无任何章节命中：整本单章，保留全部内容（不阻断导入）
    if (bestChapterRule == nullptr) {
        BookChapter whole;
        whole.index = 1;
        whole.title = QFileInfo(path).completeBaseName();
        whole.startLine = 0;
        whole.endLine = lines.size();
        result.chapters.append(whole);
        result.ok = true;
        result.warnings.append(QStringLiteral("未识别到章节标题，已按整本单章导入"));
        return result;
    }
    result.ruleId = bestChapterRule->id;

    // 3. 一次扫描收集全部标记（卷优先于章：同一行只归入卷）
    QList<Marker> markers;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (!isHeadingCandidate(line))
            continue;

        bool matched = false;
        for (const Rule &rule : parsedRules) {
            if (rule.level != kLevelVolume)
                continue;
            if (rule.pattern.match(line).hasMatch()) {
                markers.append(Marker{i, true, line.trimmed()});
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        if (bestChapterRule->pattern.match(line).hasMatch())
            markers.append(Marker{i, false, line.trimmed()});
    }

    // 4. 按标记切分：章标记开新章，卷标记只切换归属
    int currentVolume = 0;
    QString currentVolumeName;
    BookChapter current;
    bool hasCurrent = false;
    int prefaceEnd = markers.isEmpty() ? 0 : markers.first().line;

    auto closeChapter = [&](int endLine) {
        current.endLine = endLine;
        bool hasContent = false;
        sliceLines(lines, current.startLine, current.endLine, &hasContent);
        // 标题行本身已计入区间；正文为空（如两个标题紧邻）时也保留，避免章节序号断档
        current.index = result.chapters.size() + 1;
        result.chapters.append(current);
    };

    // 开篇：首个标记之前若有内容，单独成章
    if (prefaceEnd > 0) {
        bool hasContent = false;
        sliceLines(lines, 0, prefaceEnd, &hasContent);
        if (hasContent) {
            BookChapter preface;
            preface.title = kPrefaceTitle;
            preface.startLine = 0;
            preface.endLine = prefaceEnd;
            preface.index = 1;
            result.chapters.append(preface);
            result.warnings.append(
                QStringLiteral("首个章节标题之前的内容已作为「%1」保留").arg(kPrefaceTitle));
        }
    }

    for (const Marker &marker : markers) {
        if (marker.volume) {
            // 卷标题行不属于任何一章：先结束当前章，避免把卷名吞进上一章正文
            if (hasCurrent) {
                closeChapter(marker.line);
                hasCurrent = false;
            }
            ++currentVolume;
            currentVolumeName = marker.title;
            continue;
        }
        if (hasCurrent)
            closeChapter(marker.line);
        current = BookChapter();
        current.title = marker.title;
        current.volume = currentVolume;
        current.volumeName = currentVolumeName;
        current.startLine = marker.line;
        hasCurrent = true;
    }
    if (hasCurrent)
        closeChapter(lines.size());

    if (result.chapters.isEmpty()) {
        result.error = QStringLiteral("切分结果为空");
        return result;
    }

    result.ok = true;
    return result;
}

bool BookSplitter::writePackage(const BookSourceInfo &source, const BookAnalysis &analysis,
                                const QString &targetDir, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error != nullptr)
            *error = message;
        return false;
    };

    if (!analysis.ok)
        return fail(analysis.error.isEmpty() ? QStringLiteral("切分结果不可用") : analysis.error);

    QFile sourceFile(source.path);
    if (!sourceFile.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("源文件无法读取：%1").arg(QDir::toNativeSeparators(source.path)));
    const QByteArray bytes = sourceFile.readAll();
    sourceFile.close();

    QString encoding;
    const QString text = decodeText(bytes, &encoding);
    if (text.isEmpty())
        return fail(QStringLiteral("文本解码失败，无法写出章节"));
    const QStringList lines = splitLines(text);

    QJsonArray chapters;
    QSet<int> seenVolumes;
    QJsonArray volumes;

    for (const BookChapter &chapter : analysis.chapters) {
        const QString fileName = QString::number(chapter.index) + QStringLiteral(".txt");
        const QString targetPath = targetDir + QLatin1Char('/') + fileName;

        QString content = sliceLines(lines, chapter.startLine, chapter.endLine, nullptr);
        // 至少保留标题行，避免出现 0 字节章节
        if (content.isEmpty())
            content = chapter.title + QLatin1Char('\n');

        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(QStringLiteral("章节文件写入失败：%1").arg(QDir::toNativeSeparators(targetPath)));
        out.write(content.toUtf8());
        out.close();

        chapters.append(QJsonObject{
            {QStringLiteral("file"), fileName},
            {QStringLiteral("title"), chapter.title},
            {QStringLiteral("volume"), chapter.volume},
        });

        if (chapter.volume > 0 && !seenVolumes.contains(chapter.volume)) {
            seenVolumes.insert(chapter.volume);
            volumes.append(QJsonObject{
                {QStringLiteral("index"), chapter.volume},
                {QStringLiteral("name"), chapter.volumeName},
            });
        }
    }

    const QJsonObject manifest{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("chapterCount"), analysis.chapters.size()},
        {QStringLiteral("rule"), analysis.ruleId},
        {QStringLiteral("volumes"), volumes},
        {QStringLiteral("chapters"), chapters},
        {QStringLiteral("source"),
         QJsonObject{
             {QStringLiteral("name"), source.name},
             {QStringLiteral("sha256"), source.sha256},
             {QStringLiteral("encoding"), encoding},
         }},
    };

    QFile manifestFile(targetDir + QLatin1Char('/') + kManifestName);
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(QStringLiteral("包清单写入失败"));
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();
    return true;
}
