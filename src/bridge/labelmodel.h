#ifndef LABELMODEL_H
#define LABELMODEL_H

#include <QObject>
#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QVector>

/**
 * @brief 标签数据模型
 * @details 负责管理标签的增删查改，以及标签间关系的管理
 */
class LabelModel : public QAbstractListModel
{
    Q_OBJECT

    // 标签总数属性
    Q_PROPERTY(int labelCount READ labelCount NOTIFY labelCountChanged)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit LabelModel(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~LabelModel();

    //=============== QAbstractListModel 接口 ===============

    /**
     * @brief 返回列表项数量
     * @param parent 父索引
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

    //=============== 标签操作接口 ===============

    /**
     * @brief 刷新模型数据
     * @param dbPath 数据库路径
     */
    Q_INVOKABLE void refresh(const QString &dbPath);

    /**
     * @brief 添加标签
     * @param name 标签名称
     * @param color 标签颜色
     * @return 是否成功
     */
    Q_INVOKABLE bool addLabel(const QString &name, const QString &color = "#000000");

    /**
     * @brief 删除标签
     * @param labelId 标签ID
     * @return 是否成功
     */
    Q_INVOKABLE bool removeLabel(int labelId);

    /**
     * @brief 更新标签
     * @param labelId 标签ID
     * @param name 新名称
     * @param color 新颜色
     * @return 是否成功
     */
    Q_INVOKABLE bool updateLabel(int labelId, const QString &name, const QString &color);

    /**
     * @brief 添加标签关系
     * @param labelId1 标签1 ID
     * @param labelId2 标签2 ID
     * @param relationType 关系类型
     * @return 是否成功
     */
    Q_INVOKABLE bool addRelation(int labelId1, int labelId2, const QString &relationType);

    /**
     * @brief 删除标签关系
     * @param labelId1 标签1 ID
     * @param labelId2 标签2 ID
     * @return 是否成功
     */
    Q_INVOKABLE bool removeRelation(int labelId1, int labelId2);

    /**
     * @brief 获取标签的所有关系
     * @param labelId 标签ID
     * @return 关系列表
     */
    Q_INVOKABLE QVariantList getLabelRelations(int labelId);

    /**
     * @brief 获取文件的所有标签
     * @param fileId 文件ID
     * @return 标签ID列表
     */
    Q_INVOKABLE QVariantList getFileLabels(int fileId);

    /**
     * @brief 设置文件的标签
     * @param fileId 文件ID
     * @param labelIds 标签ID列表
     * @return 是否成功
     */
    Q_INVOKABLE bool setFileLabels(int fileId, const QVariantList &labelIds);

    /**
     * @brief 获取标签总数
     * @return 标签数量
     */
    int labelCount() const { return m_labels.size(); }

signals:
    /**
     * @brief 标签数量变化信号
     */
    void labelCountChanged();

private:
    /**
     * @brief 标签数据结构
     */
    struct LabelItem {
        int id;              // 标签ID
        QString name;       // 标签名称
        QString color;      // 标签颜色
    };

    /**
     * @brief 标签关系结构
     */
    struct RelationItem {
        int labelId1;       // 标签1 ID
        int labelId2;       // 标签2 ID
        QString relationType;  // 关系类型
    };

    /**
     * @brief 角色枚举
     */
    enum Roles {
        IdRole = Qt::UserRole + 1,   // ID角色
        NameRole,                     // 名称角色
        ColorRole                     // 颜色角色
    };

    QVector<LabelItem> m_labels;      // 标签列表
    QVector<RelationItem> m_relations; // 关系列表
    QSqlDatabase dbLink;              // 数据库连接
};

#endif // LABELMODEL_H
