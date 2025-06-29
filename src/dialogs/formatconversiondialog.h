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

class FormatConversionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FormatConversionDialog(const QStringList &selectedFiles, QWidget *parent = nullptr);

private:
    QStringList m_selectedFiles;
    QComboBox *m_originalFormatCombo;
    QComboBox *m_targetFormatCombo;
    QListWidget *m_fileListWidget;
    QPushButton *m_convertButton;

    void setupUI();             // GUI构建
    void populateComboBoxes();
    void performConversion();

private slots:
    void onConvertClicked();
};

#endif // FORMATCONVERSIONDIALOG_H
