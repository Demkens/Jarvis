// 媒体浏览对话框
#ifndef MEDIABROWSER_H
#define MEDIABROWSER_H

#include <QWidget>
#include <QDialog>
#include <QDir>
#include <QScrollArea>
#include <QGridLayout>
#include <QFileInfo>
#include <QLabel>

class MediaBrowser : public QDialog
{
    Q_OBJECT

public:
    explicit MediaBrowser(const QString &directory, QWidget *parent = nullptr);
};

#endif // MEDIABROWSER_H
