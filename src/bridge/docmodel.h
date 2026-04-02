#ifndef DOCMODEL_H
#define DOCMODEL_H

#include <QObject>
#include <QSqlDatabase>
#include <QVector>
#include <QDateTime>

/**
 * @brief 文档数据模型
 * @details 负责管理文档的增删查改，以及文档与文件的关联管理
 */
class DocModel : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit DocModel(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~DocModel();

    //=============== 文档操作接口 ===============

    /**
     * @brief 刷新模型数据
     * @param dbPath 数据库路径
     */
    Q_INVOKABLE void refresh(const QString &dbPath);

    /**
     * @brief 创建文档
     * @param title 文档标题
     * @param content 文档内容
     * @param linkedFileId 关联文件ID（可选）
     * @return 是否成功
     */
    Q_INVOKABLE bool createDoc(const QString &title, const QString &content, int linkedFileId = -1);

    /**
     * @brief 更新文档
     * @param docId 文档ID
     * @param title 新标题
     * @param content 新内容
     * @return 是否成功
     */
    Q_INVOKABLE bool updateDoc(int docId, const QString &title, const QString &content);

    /**
     * @brief 删除文档
     * @param docId 文档ID
     * @return 是否成功
     */
    Q_INVOKABLE bool deleteDoc(int docId);

    /**
     * @brief 获取文档详情
     * @param docId 文档ID
     * @return 文档信息映射
     */
    Q_INVOKABLE QVariantMap getDoc(int docId);

    /**
     * @brief 获取所有文档
     * @return 文档列表
     */
    Q_INVOKABLE QVariantList getAllDocs();

    /**
     * @brief 获取文件的关联文档
     * @param fileId 文件ID
     * @return 文档列表
     */
    Q_INVOKABLE QVariantList getFileDocs(int fileId);

    /**
     * @brief 关联文档到文件
     * @param docId 文档ID
     * @param fileId 文件ID
     * @return 是否成功
     */
    Q_INVOKABLE bool linkDocToFile(int docId, int fileId);

    /**
     * @brief 取消文档与文件的关联
     * @param docId 文档ID
     * @param fileId 文件ID
     * @return 是否成功
     */
    Q_INVOKABLE bool unlinkDocFromFile(int docId, int fileId);

private:
    /**
     * @brief 文档数据结构
     */
    struct DocItem {
        int id;                 // 文档ID
        QString title;         // 文档标题
        QString content;       // 文档内容
        int linkedFileId;      // 关联文件ID
        QDateTime createTime;  // 创建时间
        QDateTime updateTime;  // 更新时间
    };

    QVector<DocItem> m_docs;   // 文档列表
    QSqlDatabase dbLink;       // 数据库连接
};

#endif // DOCMODEL_H
