#ifndef DBMODEL_H
#define DBMODEL_H

#include <QObject>
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

class DatabaseModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentDbName READ getCurrentDbName WRITE setCurrentDbName NOTIFY currentDbNameChanged FINAL)

public:
    explicit DatabaseModel(QObject *parent = nullptr);
    ~DatabaseModel();

    Q_INVOKABLE bool createDatabase(const QString& directory, const QString& dbName);

    QString getCurrentDbName() const;
    void setCurrentDbName(const QString &newCurrentDbName);

signals:
    void currentDbNameChanged();

private:
    QSqlDatabase dbLink;    // 数据库链接，构造函数赋值，析构函数销毁
    QString currentDbName;  // 当前打开的数据库名
};

#endif // DBMODEL_H
