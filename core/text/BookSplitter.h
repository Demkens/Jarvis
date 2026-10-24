#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

// 小说整本 txt 的切分器（M10）：解码、按规则表识别卷章、写出章文件与包清单。
// 职责边界：
//  - 只做纯文本处理，不碰数据库、不决定包 id、不写实际库（落盘由 StorageService 负责）；
//  - 规则表由类型包 type.json 的 chapterRules 提供，本类不做任何格式假设；
//  - 输出「章节在源文件中的行区间」，实际写文件由 writePackage 按区间切片。
// 高内聚说明：编码探测、标题识别、切片、清单生成本就是同一件事的四个步骤，
// 集中在此一处，避免 ImportService 与 PackageViewContext 各写一份解码逻辑。

// 一个章节：index 决定包内文件名 "<index>.txt"，startLine/endLine 为源文件行区间 [start, end)
struct BookChapter
{
    int index = 0;
    QString title;       // 原文标题行（去首尾空白后原样保留）
    int volume = 0;      // 0 = 不属于任何卷
    QString volumeName;  // 所属卷名（卷标记行原文）；volume = 0 时为空
    int startLine = 0;
    int endLine = 0;
};

// 切分分析结果（导入预览与落盘共用）
struct BookAnalysis
{
    bool ok = false;
    QString error;                 // ok = false 时的可读原因
    QString encoding;              // 实际使用的解码："UTF-8" / "GB18030"
    QString ruleId;                // 命中的章节规则 id；未命中为空
    QList<BookChapter> chapters;
    QStringList warnings;          // 不阻断导入的提示（编码回退、未识别到章节等）
};

// 写出包内文件所需的源文件信息
struct BookSourceInfo
{
    QString path;         // 源文件绝对路径
    QString name;         // 源文件名（写入 config.txt 便于追溯）
    QString sha256;       // 源文件 SHA-256（用于判断包与源是否一致）
    QString encoding;     // analyze() 返回的编码
};

// 小说文本切分器
class BookSplitter
{
public:
    // 函数: 字节流 → 文本（UTF-8 严格校验，失败回退 GB18030）
    // 输入: bytes —— 原始字节；encoding —— 可选出参，返回实际编码名
    // 输出: 解码后的文本；两类解码都失败时返回空串
    static QString decodeText(const QByteArray &bytes, QString *encoding = nullptr);

    // 函数: 分析整本 txt，产出章节清单
    // 输入: path —— 源 txt 绝对路径；rules —— type.json 的 chapterRules
    //       （每项为 {id, level: "chapter"|"volume", pattern}，顺序即优先级）
    // 输出: BookAnalysis；ok = false 时 error 说明原因（文件不可读 / 规则表非法）
    static BookAnalysis analyze(const QString &path, const QVariantList &rules);

    // 函数: 按分析结果写出章节文件与 config.txt
    // 输入: source —— 源文件信息；analysis —— analyze() 的结果；
    //       targetDir —— 已存在的空目录（导入临时区）
    // 输出: 成功返回 true；失败返回 false 并以 error 说明原因
    static bool writePackage(const BookSourceInfo &source, const BookAnalysis &analysis,
                             const QString &targetDir, QString *error);
};
