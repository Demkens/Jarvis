#ifndef FILEMODEL_H
#define FILEMODEL_H

#include <QObject>
#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QVector>
#include <QDateTime>

/**
 * @brief 文件数据模型
 * @details 负责管理文件资源的CRUD操作，与files表交互
 */
class FileModel : public QAbstractListModel
{
    Q_OBJECT

    // 文件总数属性
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit FileModel(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~FileModel();

    //=============== QAbstractListModel 接口 ===============

    /**
     * @brief 返回列表项数量
     * @param parent 父索引（用于树模型，此处忽略）
     * @return 列表项数量
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 返回指定索引的数据
     * @param index 模型索引
     * @param role 数据角色
     * @return 对应角色的数据
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 返回角色名称映射
     * @return 角色名到角色ID的映射
     */
    QHash<int, QByteArray> roleNames() const override;

    //=============== 数据操作接口 ===============

    /**
     * @brief 刷新模型数据
     * @param dbPath 数据库路径
     */
    Q_INVOKABLE void refresh(const QString &dbPath);

    /**
     * @brief 添加文件
     * @param filePath 文件路径
     * @param fileType 文件类型
     * @return 是否成功
     */
    Q_INVOKABLE bool addFile(const QString &filePath, const QString &fileType);

    /**
     * @brief 删除文件
     * @param fileId 文件ID
     * @return 是否成功
     */
    Q_INVOKABLE bool removeFile(int fileId);

    /**
     * @brief 更新文件信息
     * @param fileId 文件ID
     * @param name 新名称
     * @param path 新路径
     * @return 是否成功
     */
    Q_INVOKABLE bool updateFile(int fileId, const QString &name, const QString &path);

    /**
     * @brief 获取文件详情
     * @param fileId 文件ID
     * @return 文件信息映射
     */
    Q_INVOKABLE QVariantMap getFile(int fileId) const;

    /**
     * @brief 获取文件总数
     * @return 文件数量
     */
    int fileCount() const { return m_files.size(); }

signals:
    /**
     * @brief 文件数量变化信号
     */
    void fileCountChanged();

private:
    /**
     * @brief 文件数据结构
     */
    struct FileItem {
        int id;                 // 文件ID
        QString name;           // 文件名
        QString path;           // 文件路径
        QString type;           // 文件类型
        QString thumbnail;      // 缩略图路径
        QDateTime createTime;  // 创建时间
    };

    /**
     * @brief 角色枚举
     */
    enum Roles {
        IdRole = Qt::UserRole + 1,      // ID角色
        NameRole,                        // 名称角色
        PathRole,                        // 路径角色
        TypeRole,                        // 类型角色
        ThumbnailRole,                   // 缩略图角色
        CreateTimeRole                   // 创建时间角色
    };

    QVector<FileItem> m_files;   // 文件列表
    QSqlDatabase dbLink;         // 数据库连接
};

#endif // FILEMODEL_H
