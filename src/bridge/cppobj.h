// 工厂类，负责创建各模型实例
#ifndef CPPOBJ_H
#define CPPOBJ_H

#include <QObject>

class FileModel;
class LabelModel;
class DataCounterModel;
class DocModel;

class CppObj : public QObject
{
    Q_OBJECT

public:
    explicit CppObj(QObject *parent = nullptr);

    Q_INVOKABLE FileModel* createFileModel(QObject *parent = nullptr);
    Q_INVOKABLE LabelModel* createLabelModel(QObject *parent = nullptr);
    Q_INVOKABLE DataCounterModel* createDataCounterModel(QObject *parent = nullptr);
    Q_INVOKABLE DocModel* createDocModel(QObject *parent = nullptr);

signals:

};

#endif // CPPOBJ_H
