#ifndef CPPOBJ_H
#define CPPOBJ_H

#include <QObject>

class CppObj : public QObject
{
    Q_OBJECT
public:
    explicit CppObj(QObject *parent = nullptr);

signals:
};

#endif // CPPOBJ_H
