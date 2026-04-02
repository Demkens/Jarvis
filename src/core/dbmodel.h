#ifndef DBMODEL_H
#define DBMODEL_H

#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

class DatabaseModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString currentDbName READ getCurrentDbName WRITE setCurrentDbName NOTIFY currentDbNameChanged FINAL)

public:
    explicit DatabaseModel(QObject *parent = nullptr);
    ~DatabaseModel();

    // 必须重写的虚函数
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool createDatabase(const QString& directory, const QString& dbName);
    Q_INVOKABLE void refresh();

    QString getCurrentDbName() const;
    void setCurrentDbName(const QString &newCurrentDbName);

signals:
    void currentDbNameChanged();

private:
    struct DataItem {
        int id;
        QString name;
        double value;
    };
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ValueRole
    };
    QSqlDatabase dbLink;    // 数据库链接，构造函数赋值，析构函数销毁
    QString currentDbName;  // 当前打开的数据库名
    QVector<DataItem> m_data;
};

#endif // DBMODEL_H