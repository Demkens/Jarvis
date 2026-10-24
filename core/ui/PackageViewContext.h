#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

class AppPaths;
class LibraryService;
class PackageService;

// 查看器上下文（开发文档 7.3）：TypeEngine 加载 viewer.qml 时注入的唯一对象。
// 类型包 QML 经它访问包数据，不直接开数据库、不直接碰文件系统根：
//   packageId / title / typeForm
//   files：包内内容文件清单（相对路径、file:// URL、大小），递归列出子目录、按相对路径排序；
//          config.txt 等包清单不进入清单，需要时用 readText() 读取
//   readText()：读取包内任意文本文件（小说正文、包清单）
//   positionJson / savePosition：浏览记忆（{"index": n} / {"chapter": n, "scroll": y} 等，
//                                语义由类型自定义）
// 注：类型无 DB 侧私有字段，故不再暴露字段读写接口。
class PackageViewContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 packageId READ packageId CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY dataChanged)
    Q_PROPERTY(QString typeForm READ typeForm CONSTANT)
    Q_PROPERTY(QVariantList files READ files CONSTANT)
    // 包清单（config.txt）解析出的章节：整本文本包为
    //   [{file, title, volume, volumeName}]，卷名已按 volume 展开；
    // 非文本包或清单缺失时为空列表（类型页应退化为按 files 顺序处理）
    Q_PROPERTY(QVariantList chapters READ chapters CONSTANT)
    // 包清单中的卷：[{index, name}]；无卷结构时为空
    Q_PROPERTY(QVariantList volumes READ volumes CONSTANT)
    Q_PROPERTY(QString positionJson READ positionJson WRITE setPositionJson
                   NOTIFY positionChanged)

public:
    PackageViewContext(qint64 packageId, AppPaths *paths, LibraryService *library,
                       PackageService *packages, QObject *parent = nullptr);

    qint64 packageId() const { return m_packageId; }
    QString title() const { return m_title; }
    QString typeForm() const { return m_typeForm; }
    QVariantList files() const { return m_files; }
    QVariantList chapters() const { return m_chapters; }
    QVariantList volumes() const { return m_volumes; }

    QString positionJson() const { return m_position; }
    void setPositionJson(const QString &json);

    // 保存浏览位置（查看器翻页/定时调用）；不触发 PackageUpdated
    Q_INVOKABLE bool savePosition();

    // 函数: 读取包内文本文件
    // 输入: relativePath —— 相对包根的路径（'/' 分隔，不得为绝对路径或含 ".."）
    // 输出: 解码后的文本（UTF-8 优先、GB18030 兜底）；路径非法、文件不存在或
    //       解码失败时返回空字符串，并以 qWarning 记录原因
    Q_INVOKABLE QString readText(const QString &relativePath) const;

signals:
    void dataChanged();
    void positionChanged();

private:
    void load();
    // 解析包内 config.txt 到 chapters/volumes（清单缺失或损坏时留空）
    void loadManifest();

    qint64 m_packageId;
    AppPaths *m_paths;
    LibraryService *m_library;
    PackageService *m_packages;

    QString m_title;
    QString m_typeForm;
    QVariantList m_files;
    QVariantList m_chapters;
    QVariantList m_volumes;
    QString m_position;
    QString m_packageDir;  // 包根目录绝对路径（readText 的解析基准）
};
