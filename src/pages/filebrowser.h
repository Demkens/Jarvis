// 文件浏览器主页面
#ifndef MEDIABROWSER_H
#define MEDIABROWSER_H

#include <QWidget>
#include <QDialog>
#include <QDir>
#include <QScrollArea>
#include <QGridLayout>
#include <QFileInfo>
#include <QLabel>

class FileBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowser(const QFileInfo &fileInfo, QWidget *parent = nullptr);
};

#endif // MEDIABROWSER_H
