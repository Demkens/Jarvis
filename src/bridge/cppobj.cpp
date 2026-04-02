#include "cppobj.h"
#include "filemodel.h"
#include "labelmodel.h"
#include "datacountermodel.h"
#include "docmodel.h"

CppObj::CppObj(QObject *parent)
    : QObject{parent}
{
}

FileModel* CppObj::createFileModel(QObject *parent)
{
    return new FileModel(parent);
}

LabelModel* CppObj::createLabelModel(QObject *parent)
{
    return new LabelModel(parent);
}

DataCounterModel* CppObj::createDataCounterModel(QObject *parent)
{
    return new DataCounterModel(parent);
}

DocModel* CppObj::createDocModel(QObject *parent)
{
    return new DocModel(parent);
}

