// 格式转换对话框
#ifndef FORMATCONVERSIONDIALOG_H
#define FORMATCONVERSIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QProgressDialog>

class FormatConversion : public QDialog
{
    Q_OBJECT

public:
    explicit FormatConversion(const QStringList &selectedFiles, QWidget *parent = nullptr);

private:
    // 容器和组件
    QComboBox *m_originalFormatCombo;
    QComboBox *m_targetFormatCombo;
    QPushButton *m_convertButton;
    QListWidget *m_fileListWidget;

    // 数据列表
    QStringList m_selectedFiles;

    void setupUI();             // GUI构建
    void populateComboBoxes();  // 设置格式
    void performConversion();   // 核心转换函数

private slots:
    void onConvertClicked();
};

#endif // FORMATCONVERSIONDIALOG_H
