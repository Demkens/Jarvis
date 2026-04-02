#ifndef DATACOUNTERMODEL_H
#define DATACOUNTERMODEL_H

#include <QObject>
#include <QSqlDatabase>

/**
 * @brief 数据统计模型
 * @details 负责从多维度统计数据信息，包括文件类型统计、时间线统计、标签分布等
 */
class DataCounterModel : public QObject
{
    Q_OBJECT

    // 统计属性
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY statsChanged)
    Q_PROPERTY(int totalLabels READ totalLabels NOTIFY statsChanged)
    Q_PROPERTY(int totalSize READ totalSize NOTIFY statsChanged)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit DataCounterModel(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~DataCounterModel();

    //=============== 统计操作接口 ===============

    /**
     * @brief 刷新统计数据
     * @param dbPath 数据库路径
     */
    Q_INVOKABLE void refresh(const QString &dbPath);

    /**
     * @brief 获取文件类型统计
     * @return 类型到数量的映射
     */
    Q_INVOKABLE QVariantMap getFileTypeStats();

    /**
     * @brief 获取时间线统计
     * @return 日期到数量的映射
     */
    Q_INVOKABLE QVariantMap getTimeLineStats();

    /**
     * @brief 获取标签分布统计
     * @return 标签名到文件数量的映射
     */
    Q_INVOKABLE QVariantMap getLabelDistribution();

    /**
     * @brief 获取文件总数
     * @return 文件数量
     */
    int totalFiles() const { return m_totalFiles; }

    /**
     * @brief 获取标签总数
     * @return 标签数量
     */
    int totalLabels() const { return m_totalLabels; }

    /**
     * @brief 获取总大小
     * @return 文件总大小
     */
    qint64 totalSize() const { return m_totalSize; }

signals:
    /**
     * @brief 统计变化信号
     */
    void statsChanged();

private:
    int m_totalFiles;     // 文件总数
    int m_totalLabels;    // 标签总数
    qint64 m_totalSize;   // 文件总大小
    QSqlDatabase dbLink;  // 数据库连接
};

#endif // DATACOUNTERMODEL_H
