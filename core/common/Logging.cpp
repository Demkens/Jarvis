#include "Logging.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

namespace {

// 日志目录与按日滚动的当前文件（消息处理器为全局函数，状态以文件级静态变量持有）
QString g_logDir;
QFile g_logFile;
QDate g_logDate;
int g_thresholdRank = 1; // 默认 info（见 rankOf 的排序）

// 级别权重：与 QtMsgType 的枚举数值无关，须显式排序（QtInfoMsg 的数值反而最大）
// 输入: type —— Qt 消息类型；输出: 0=debug 1=info 2=warning 3=critical 4=fatal
int rankOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
        return 3;
    case QtFatalMsg:
        return 4;
    }
    return 0;
}

// 级别显示名（日志行内标记）
// 输入: type —— Qt 消息类型；输出: 大写级别名
const char *nameOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "UNKNOWN";
}

// 环境变量 → 阈值权重；未设置或非法回退 info
// 输入: 无（读 JARVIS_LOG_LEVEL）；输出: 阈值权重
int thresholdFromEnv()
{
    const QString level =
        qEnvironmentVariable("JARVIS_LOG_LEVEL").trimmed().toLower();
    if (level == QLatin1String("debug"))
        return 0;
    if (level == QLatin1String("info"))
        return 1;
    if (level == QLatin1String("warning") || level == QLatin1String("warn"))
        return 2;
    if (level == QLatin1String("critical") || level == QLatin1String("error"))
        return 3;
    return 1;
}

// 打开（或跨日切换到）当日日志文件
// 输入: date —— 目标日期；输出: 无（失败仅置 g_logFile 无效，由调用方降级）
void openForDate(const QDate &date)
{
    g_logDate = date;
    g_logFile.close();
    if (g_logDir.isEmpty())
        return;
    const QString fileName = QStringLiteral("jarvis-%1.log")
                                 .arg(date.toString(QStringLiteral("yyyy-MM-dd")));
    g_logFile.setFileName(QDir(g_logDir).filePath(fileName));
    // 追加写：同日多次启动不清空既有日志
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

// 统一消息处理器：过滤 → 组装结构化行 → stderr + 按日滚动落盘
// 输入: type —— 消息级别；context —— 源码位置/分类；message —— 消息正文
// 输出: 无；FatalMsg 在落盘并刷新后终止进程（保持 Qt 默认语义）
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const int rank = rankOf(type);
    if (rank < g_thresholdRank)
        return; // 低于阈值的级别整体丢弃（既不落盘也不回显）

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QDateTime now = QDateTime::currentDateTime();
    if (!g_logDate.isValid() || g_logDate.toString(QStringLiteral("yyyy-MM-dd")) != today)
        openForDate(QDate::currentDate());

    // 组装结构化行：时间 级别 [文件:行] [分类] 正文（上下文缺失项自动略去，如 QML 无 C++ 行号）
    QString line = now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    line += QStringLiteral(" [%1]").arg(QLatin1String(nameOf(type)));
    if (context.file != nullptr) {
        // context.file 是 UTF-8 源码路径；只取文件名，避免整条路径把日志行撑长
        const QString sourceFile = QFileInfo(QString::fromUtf8(context.file)).fileName();
        line += QStringLiteral(" [%1:%2]").arg(sourceFile).arg(context.line);
    }
    if (context.category != nullptr && qstrcmp(context.category, "default") != 0)
        line += QStringLiteral(" [%1]").arg(QLatin1String(context.category));
    line += QStringLiteral(" ") + message;

    // stderr 回显：按本地代码页输出（与 Qt 默认 handler 的 toLocal8Bit 取向一致）。
    // 若在此用 toUtf8()，Windows 中文环境的控制台/IDE 面板会按 CP936 解码 UTF-8 字节 → 中文乱码。
    // 日志文件仍用 UTF-8（见下方 QTextStream，Qt 6 默认编码），保证文件跨平台可读、不受本机代码页影响。
    const QByteArray local = line.toLocal8Bit();
    std::fwrite(local.constData(), 1, static_cast<size_t>(local.size()), stderr);
    std::fwrite("\n", 1, 1, stderr);

    if (g_logFile.isOpen()) {
        QTextStream out(&g_logFile);
        out << line << '\n';
        out.flush(); // 崩溃/强退场景下尽量保留已产生日志
    }

    if (type == QtFatalMsg)
        abort();
}

} // namespace

void Logging::install(const QString &envsRoot)
{
    g_thresholdRank = thresholdFromEnv();

    // 目录准备独立于阈值：即使 debug 阈值也先建好目录，避免运行中切换级别时无处可写
    QDir root(envsRoot);
    if (!root.exists())
        root.mkpath(QStringLiteral("."));
    if (root.mkpath(QStringLiteral("logs")))
        g_logDir = root.filePath(QStringLiteral("logs"));

    qInstallMessageHandler(messageHandler);

    if (g_logDir.isEmpty()) {
        qWarning().noquote()
            << "[logging] 日志目录不可用，日志仅输出到 stderr："
            << root.filePath(QStringLiteral("logs"));
        return;
    }
    openForDate(QDate::currentDate());
    if (!g_logFile.isOpen()) {
        qWarning().noquote()
            << "[logging] 日志文件打开失败，仅输出到 stderr：" << g_logFile.fileName();
        g_logDir.clear();
        return;
    }
    qInfo().noquote() << "[logging] 日志已启用 →" << g_logFile.fileName()
                      << "（级别阈值 JARVIS_LOG_LEVEL，默认 info）";
}
