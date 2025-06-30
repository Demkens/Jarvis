#include "formatconversiondialog.h"

FormatConversionDialog::FormatConversionDialog(const QStringList &selectedFiles, QWidget *parent)
    : QDialog(parent), m_selectedFiles(selectedFiles)
{
    this->setWindowTitle("文件格式转换");
    this->setMinimumSize(500, 400);
    this->setStyleSheet("QDialog { background-color: #f5f5f5; }"
                         "QGroupBox { border: 1px solid #ddd; border-radius: 5px; margin-top: 1ex; }"
                         "QGroupBox::title { subcontrol-origin: margin; padding: 0 5px; }"
                         "QComboBox, QPushButton, QListWidget { border: 1px solid #ccc; border-radius: 3px; padding: 5px; }"
                         "QPushButton { background-color: #4CAF50; color: white; min-width: 80px; }"
                         "QPushButton:hover { background-color: #45a049; }");

    setupUI();
    populateComboBoxes();
}

void FormatConversionDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 文件列表显示
    QLabel *filesLabel = new QLabel("选中的文件:");
    filesLabel->setStyleSheet("font-weight: bold; margin-bottom: 5px;");
    mainLayout->addWidget(filesLabel);

    m_fileListWidget = new QListWidget(this);
    m_fileListWidget->setSelectionMode(QAbstractItemView::NoSelection);     // 禁用列表控件的项选择功能
    m_fileListWidget->addItems(m_selectedFiles);
    mainLayout->addWidget(m_fileListWidget);

    // 格式选择
    QLabel *formatLabel = new QLabel("选择转换格式:");
    formatLabel->setStyleSheet("font-weight: bold; margin-top: 15px; margin-bottom: 5px;");
    mainLayout->addWidget(formatLabel);

    QHBoxLayout *formatLayout = new QHBoxLayout();

    QLabel *originalLabel = new QLabel("原始格式:");
    m_originalFormatCombo = new QComboBox(this);
    formatLayout->addWidget(originalLabel);
    formatLayout->addWidget(m_originalFormatCombo);

    QLabel *targetLabel = new QLabel("目标格式:");
    m_targetFormatCombo = new QComboBox(this);
    formatLayout->addWidget(targetLabel);
    formatLayout->addWidget(m_targetFormatCombo);

    mainLayout->addLayout(formatLayout);

    // 转换按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton *cancelButton = new QPushButton("取消");
    connect(cancelButton, &QPushButton::clicked, this, &FormatConversionDialog::reject);
    buttonLayout->addWidget(cancelButton);

    m_convertButton = new QPushButton("转换");
    m_convertButton->setStyleSheet("background-color: #2196F3;");
    connect(m_convertButton, &QPushButton::clicked, this, &FormatConversionDialog::onConvertClicked);
    buttonLayout->addWidget(m_convertButton);

    mainLayout->addLayout(buttonLayout);
}

void FormatConversionDialog::populateComboBoxes()
{
    // 收集所有选中的文件格式
    QSet<QString> formats;
    for (const QString &file : m_selectedFiles)
    {
        QFileInfo info(file);
        if (info.isFile())
        {
            formats.insert(info.suffix().toLower());
        }
    }

    // 添加到组合框
    m_originalFormatCombo->addItems(formats.values());

    // 添加常用目标格式
    m_targetFormatCombo->addItem("png");
    m_targetFormatCombo->addItem("jpg");
    m_targetFormatCombo->addItem("jpeg");
    m_targetFormatCombo->addItem("bmp");
    m_targetFormatCombo->addItem("gif");
    m_targetFormatCombo->addItem("webp");

    // 默认选择第一个格式
    if (m_originalFormatCombo->count() > 0)
    {
        m_originalFormatCombo->setCurrentIndex(0);
    }
    if (m_targetFormatCombo->count() > 0)
    {
        m_targetFormatCombo->setCurrentIndex(0);
    }
}

void FormatConversionDialog::onConvertClicked()
{
    QString originalFormat = m_originalFormatCombo->currentText().toLower();
    QString targetFormat = m_targetFormatCombo->currentText().toLower();

    if (originalFormat.isEmpty() || targetFormat.isEmpty())
    {
        QMessageBox::warning(this, "格式错误", "请选择源格式和目标格式");
        return;
    }

    if (originalFormat == targetFormat)
    {
        QMessageBox::information(this, "格式相同", "源格式和目标格式相同，无需转换");
        return;
    }

    // 确认开始转换窗口
    int result = QMessageBox::question(this, "确认转换",
        QString("确定要将所有选中的 %1 文件转换为 %2 格式吗?").arg(originalFormat).arg(targetFormat),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes)
    {
        performConversion();
    }
}

void FormatConversionDialog::performConversion()
{
    QString originalFormat = m_originalFormatCombo->currentText().toLower();
    QString targetFormat = m_targetFormatCombo->currentText().toLower();

    // 创建进度对话框
    QProgressDialog progress("正在转换文件...", "取消", 0, m_selectedFiles.size(), this);
    progress.setWindowModality(Qt::WindowModal);    // 将进度对话框设置为窗口模态, 阻塞父窗口的所有交互
    progress.setMinimumDuration(0);

    int successCount = 0;
    int skippedCount = 0;
    int errorCount = 0;

    for (int i = 0; i < m_selectedFiles.size(); i++)
    {
        progress.setValue(i);
        progress.setLabelText(QString("正在转换文件: %1/%2").arg(i+1).arg(m_selectedFiles.size()));

        if (progress.wasCanceled()) break;

        QString filePath = m_selectedFiles[i];
        QFileInfo info(filePath);

        // 检查文件格式是否匹配
        if (info.suffix().toLower() != originalFormat)
        {
            skippedCount++;
            continue;
        }

        // 创建目标文件夹路径 (原始目录/Conversion)
        QDir targetDir(info.path() + "/Conversion");
        if (!targetDir.exists())
        {
            if (!targetDir.mkpath("."))
            {
                errorCount++;
                continue;
            }
        }

        // 构建新文件路径 (直接覆盖已存在的文件)
        QString newPath = targetDir.filePath(info.completeBaseName() + "." + targetFormat);

        // 执行格式转换
        QImage image;
        if (!image.load(filePath))
        {
            errorCount++;
            continue;
        }

        // 设置转换质量
        int quality = -1; // 默认质量
        if (targetFormat == "jpg" || targetFormat == "jpeg")
        {
            quality = 95; // 设置较高的JPEG质量
        }

        // 保存为新的格式
        if (!image.save(newPath, targetFormat.toUpper().toLatin1().data(), quality))    // 说是需要转换成C语言风格的字符串，但不清楚原因
        {
            errorCount++;
            continue;
        }

        successCount++;
    }

    progress.setValue(m_selectedFiles.size());

    // 显示结果
    QString message = QString("转换完成！\n\n"
                              "成功: %1\n"
                              "跳过: %2\n"
                              "失败: %3").arg(successCount).arg(skippedCount).arg(errorCount);
    QMessageBox::information(this, "转换结果", message);

    accept();
}
