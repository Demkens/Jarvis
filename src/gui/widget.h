// 主页面
#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QStackedWidget>

#include "fileeditor.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private:
    // 容器和组件
    QHBoxLayout *hlayout;       // 水平布局
    QVBoxLayout *vlayout;       // 垂直布局
    QPushButton *btn_home;      // 主页按钮
    QPushButton *btn_file;      // 文件按钮
    QPushButton *btn_set;       // 设置按钮
    QWidget *page_home;         // 第一页-主页
    FileEditor *page_manage;    // 第二页-管理页
    QWidget *page_set;          // 第三页-设置页
    QStackedWidget *stack;      // 多页窗口

    void setupUI();             // GUI构建
    void setupConnections();    // 信号与槽连接

private:
    Ui::Widget *ui;
};
#endif // WIDGET_H
