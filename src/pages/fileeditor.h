// 文件编辑器主页面
#ifndef MANAGEPAGE_H
#define MANAGEPAGE_H

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QGroupBox>
#include <QFileSystemModel>
#include <QTableView>
#include <QFileDialog>
#include <QLabel>
#include <QGuiApplication>
#include <QScreen>
#include <QDialog>

#include "formatconversion.h"

class FileEditor : public QWidget
{
     Q_OBJECT

public:
    FileEditor(QWidget *parent = nullptr);

private:
    // 容器和组件
    QHBoxLayout *m_Lay;         // 主排列
    QVBoxLayout *f_Lay;         // 副排列
    QHBoxLayout *hlay;          // 文件载入分区排列
    QVBoxLayout *vlay;          // 功能按钮分区排列
    QGroupBox *box_file;        // 文件载入分区
    QGroupBox *box_btn;         // 功能按钮分区
    QPushButton *btn_open;      // 打开按钮
    QPushButton *btn_refresh;   // 刷新按钮
    QPushButton *btn_1;         // 功能按钮1
    QPushButton *btn_2;         // 功能按钮2
    QTableView *tableview;      // 文件显示列表窗口
    QLineEdit *address;         // 文件显示地址栏

    // 数据模型
    QFileSystemModel *model;    // 本机文件系统模型

    void setupUI();             // GUI构建
    void setupConnections();    // 信号与槽连接
    void showImagePreview(const QString &imagePath);     // 图片预览函数

private slots:
    void on_openDir_clicked();  // 选定文件目录
    void on_refresh_clicked();  // 刷新显示目录
    void on_btn1_clicked();     // 处理btn_1点击事件
    void on_tableView_doubleClicked(const QModelIndex &index);  // 双击打开文件夹或预览图片
};

#endif // MANAGEPAGE_H
