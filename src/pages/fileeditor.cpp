#include "fileeditor.h"

FileEditor::FileEditor(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

void FileEditor::setupUI()
{
    // 主布局配置
    m_Lay = new QHBoxLayout(this);
    m_Lay->setContentsMargins(12, 12, 12, 12);  // 增加外部边距
    m_Lay->setSpacing(15);                      // 设置组件间距

    // 左侧文件区域（占比80%）
    f_Lay = new QVBoxLayout();
    f_Lay->setSpacing(10);          // 设置组件间距

    // 左侧文件区域-文件操作区域美化
    box_file = new QGroupBox("文件管理器", this);
    hlay = new QHBoxLayout(box_file);
    hlay->setContentsMargins(10, 15, 10, 15);
    hlay->setSpacing(8);

    // 文件操作区域-地址栏美化
    address = new QLineEdit(box_file);
    address->setPlaceholderText("请选择目录...");    // 设置地址栏初始文字
    address->setReadOnly(true);                    // 将地址栏设计成只读状态
    address->setStyleSheet("QLineEdit { padding: 5px; border: 1px solid #cccccc; border-radius: 4px; }");

    // 文件操作区域-按钮美化
    btn_open = new QPushButton(tr("打开"), box_file);
    btn_refresh = new QPushButton(tr("刷新"), box_file);
    QString btnStyle = "QPushButton { padding: 6px 10px; border-radius: 4px; }";
    btn_open->setStyleSheet(btnStyle);
    btn_refresh->setStyleSheet(btnStyle);
    btn_open->setMinimumHeight(32);
    btn_refresh->setMinimumHeight(32);

    // 文件操作区域布局
    hlay->addWidget(address, 4);        // 地址栏占4份空间
    hlay->addWidget(btn_open, 1);
    hlay->addWidget(btn_refresh, 1);

    // 左侧文件区域-表格视图美化
    tableview = new QTableView(this);
    tableview->setFrameShape(QFrame::NoFrame);      // 设置无边框
    tableview->setSelectionMode(QAbstractItemView::ExtendedSelection);  // 允许多选
    tableview->setSelectionBehavior(QAbstractItemView::SelectRows);     // 按行选择
    tableview->setAlternatingRowColors(true);       // 设置斑马纹
    tableview->setSortingEnabled(true);             // 设置允许排序
    tableview->setStyleSheet(
            "QTableView { background-color: #f8f9fa; gridline-color: #dee2e6; }"
            "QTableView::item:selected { background-color: #e2f0ff; }"
            "QHeaderView::section { background-color: #e9ecef; padding: 4px; }");

    // 右侧功能区域（占比20%）
    box_btn = new QGroupBox("文件操作", this);
    vlay = new QVBoxLayout(box_btn);
    vlay->setContentsMargins(15, 20, 15, 20);
    vlay->setSpacing(15);

    // 右侧功能区域-功能按钮美化
    btn_1 = new QPushButton(tr("格式转换"), box_btn);
    btn_2 = new QPushButton(tr("创建信息"), box_btn);
    QString funcBtnStyle = "QPushButton { padding: 10px; border-radius: 5px; background-color: #f8f9fa; }"
                           "QPushButton:hover { background-color: #e9ecef; }";
    btn_1->setStyleSheet(funcBtnStyle);
    btn_2->setStyleSheet(funcBtnStyle);
    btn_1->setMinimumHeight(45);
    btn_2->setMinimumHeight(45);

    // 右侧功能区域布局
    vlay->addStretch(1);
    vlay->addWidget(btn_1);
    vlay->addWidget(btn_2);
    vlay->addStretch(2);    // 底部更多空白

    // 组装主布局
    f_Lay->addWidget(box_file);
    f_Lay->addWidget(tableview, 1);  // 表格占据剩余空间
    m_Lay->addLayout(f_Lay, 4);      // 左侧占80%空间
    m_Lay->addWidget(box_btn, 1);    // 右侧占20%空间

    // 设置组框样式
    QString groupBoxStyle = "QGroupBox { border: 1px solid #dee2e6; border-radius: 8px; margin-top: 1.5ex; }"
                            "QGroupBox::title { subcontrol-origin: margin; padding: 0 5px; background-color: transparent; }";
    box_file->setStyleSheet(groupBoxStyle);
    box_btn->setStyleSheet(groupBoxStyle);

    // 初始化文件模型
    model = new QFileSystemModel(this);
    tableview->setModel(model);
}

void FileEditor::setupConnections()
{
    connect(btn_open, &QPushButton::clicked, this, &FileEditor::on_openDir_clicked);        // 单击选定按钮输入文件地址
    connect(btn_refresh, &QPushButton::clicked, this, &FileEditor::on_refresh_clicked);     // 单击刷新按钮刷新文件视图
    connect(tableview, &QTableView::doubleClicked, this, &FileEditor::on_tableView_doubleClicked);  // 双击文件视图进入文件夹或预览图片
    connect(btn_1, &QPushButton::clicked, this, &FileEditor::on_btn1_clicked);
}

void FileEditor::on_openDir_clicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(
        this,                       // 父窗口
        tr("选择目录"),              // 对话框标题
        QDir::homePath(),           // 默认打开目录
        QFileDialog::ShowDirsOnly   // 只显示目录
    );

    // 检测是否为空
    if (dirPath.isEmpty())
    {
        return;
    }

    address->setText(dirPath);
    model->setRootPath(dirPath);

    QModelIndex rootIndex = model->index(dirPath);
    tableview->setRootIndex(rootIndex);
}

void FileEditor::on_refresh_clicked()
{
    QModelIndex rootIndex = model->index(address->text());
    tableview->setRootIndex(rootIndex);
}

void FileEditor::on_tableView_doubleClicked(const QModelIndex &index)
{
    // 获取双击项目的完整路径
    QString path = model->filePath(index);
    QFileInfo fileInfo(path);

    if (fileInfo.isDir())
    {
        address->setText(path);
        tableview->setRootIndex(model->index(path));
        tableview->scrollToTop();
    }
    else
    {
        // 检查是否是图片文件
        QStringList imageExtensions = {"png", "jpg", "jpeg", "bmp", "gif", "tiff", "webp"};
        QString ext = fileInfo.suffix().toLower();

        if (imageExtensions.contains(ext))
        {
            showImagePreview(path);
        }
    }
}

void FileEditor::showImagePreview(const QString &imagePath)
{
    // 创建新的预览对话框
    QDialog *currentPreviewDialog = new QDialog(this);
    currentPreviewDialog->setWindowTitle(QFileInfo(imagePath).fileName());

    // 创建图片标签
    QLabel *imageLabel = new QLabel(currentPreviewDialog);
    imageLabel->setAlignment(Qt::AlignCenter);      // 设置对其方向，等价于 Qt::AlignHCenter | Qt::AlignVCenter, 即水平方向居中+垂直方向居中

    // 设置布局
    QVBoxLayout *layout = new QVBoxLayout(currentPreviewDialog);
    layout->addWidget(imageLabel);

    // 加载图片
    QPixmap pixmap(imagePath);
    if (pixmap.isNull())
    {
        imageLabel->setText("无法加载图片");
        return;
    }

    // 调整对话框大小以适应图片（限制在屏幕范围内）
    QScreen *screen = QGuiApplication::primaryScreen();     // 获取主屏幕
    QRect screenRect = screen->availableGeometry();         // 返回屏幕可用几何图形大小

    int width = qMin(pixmap.width(), screenRect.width() - 100);     // 获取图片宽度和屏幕宽度之间较小的一个
    int height = qMin(pixmap.height(), screenRect.height() - 100);  // 获取图片高度和屏幕高度之间较小的一个

    imageLabel->setPixmap(pixmap.scaled(width, height,              // 对原始 pixmap 图像进行缩放操作, 缩放目标尺寸为 width × height
                                     Qt::KeepAspectRatio,           // 保持原始图像的宽高比例不变
                                     Qt::SmoothTransformation));    // 使用双线性插值或双三次插值算法, 产生平滑的缩放效果，减少锯齿

    // 显示对话框
    currentPreviewDialog->setAttribute(Qt::WA_DeleteOnClose);   // 关闭窗口时自动删除窗口对象, 释放内存
    currentPreviewDialog->resize(800, 600);
    currentPreviewDialog->show();
}

void FileEditor::on_btn1_clicked()
{
    // 获取选中的文件
    QItemSelectionModel *selectionModel = tableview->selectionModel();      // 返回表格视图关联的选择模型对象
    QModelIndexList selectedIndexes = selectionModel->selectedRows();       // 返回包含所有选中行索引的列表

    if (selectedIndexes.isEmpty())
    {
        QMessageBox::warning(this, "未选择文件", "请先选择一个或多个文件");
        return;
    }

    // 收集选中的文件路径，并存储到 selectedFiles 中并传给自定义的对话框类
    QStringList selectedFiles;
    for (const QModelIndex &index : selectedIndexes)
    {
        QString filePath = model->filePath(index);
        QFileInfo info(filePath);
        if (info.isFile())
        {
            selectedFiles.append(filePath);
        }
    }

    if (selectedFiles.isEmpty())
    {
        QMessageBox::warning(this, "未选择文件", "请选择文件（而不是目录）");
        return;
    }

    // 显示转换对话框
    FormatConversion dialog(selectedFiles, this);
    dialog.exec();

    // 转换完成后刷新视图
    on_refresh_clicked();
}
