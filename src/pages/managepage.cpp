#include "managepage.h"

ManagePage::ManagePage(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

void ManagePage::setupUI()
{
    // 创建控件模型（从高级到低级）
    m_Lay = new QHBoxLayout();          // 主布局

    f_Lay = new QVBoxLayout();          // 0级布局
    box_file = new QGroupBox(this);     // 1级组件
    hlay = new QHBoxLayout(box_file);   // 2级布局
    address = new QLineEdit(box_file);                   // 3级组件
    btn_open = new QPushButton(tr("选定"), box_file);     // 3级组件
    btn_refresh = new QPushButton(tr("刷新"), box_file);  // 3级组件
    tableview = new QTableView(this);   // 1级组件

    box_btn = new QGroupBox(this);      // 0级组件
    vlay = new QVBoxLayout(box_btn);    // 1级布局
    btn_1 = new QPushButton(tr("更改文件格式"), box_btn); // 2级组件
    btn_2 = new QPushButton(tr("创建信息文件"), box_btn); // 2级组件

    // 创建模型
    model = new QFileSystemModel(this);

    // 配置控件属性（集中配置）
    tableview->setFrameShape(QFrame::NoFrame);  // 设置无边框
    tableview->setSelectionMode(QAbstractItemView::ExtendedSelection);  // 允许多选
    tableview->setSelectionBehavior(QAbstractItemView::SelectRows);     // 按行选择

    // 构建布局层次（从低级到高级）
    hlay->addWidget(address);
    hlay->addWidget(btn_open);
    hlay->addWidget(btn_refresh);

    vlay->addWidget(btn_1);
    vlay->addWidget(btn_2);

    f_Lay->addWidget(box_file);
    f_Lay->addWidget(tableview);

    m_Lay->addLayout(f_Lay);
    m_Lay->addWidget(box_btn);

    // 初始化尺寸
    m_Lay->setContentsMargins(0, 0, 0, 0);  // 设置主布局的各边距为0
    setLayout(m_Lay);
}

void ManagePage::setupConnections()
{
    connect(btn_open, &QPushButton::clicked, this, &ManagePage::on_openDir_clicked);        // 单击选定按钮输入文件地址
    connect(btn_refresh, &QPushButton::clicked, this, &ManagePage::on_refresh_clicked);     // 单击刷新按钮刷新文件视图
    connect(tableview, &QTableView::doubleClicked, this, &ManagePage::on_tableView_doubleClicked);  // 双击文件视图进入文件夹或预览图片
    connect(btn_1, &QPushButton::clicked, this, &ManagePage::on_btn1_clicked);
}

void ManagePage::on_openDir_clicked()
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

    model->setRootPath(address->text());
    tableview->setModel(model);

    QModelIndex rootIndex = model->index(dirPath);
    tableview->setRootIndex(rootIndex);
}

void ManagePage::on_refresh_clicked()
{
    QModelIndex rootIndex = model->index(address->text());
    tableview->setRootIndex(rootIndex);
}

void ManagePage::on_tableView_doubleClicked(const QModelIndex &index)
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

void ManagePage::showImagePreview(const QString &imagePath)
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

void ManagePage::on_btn1_clicked()
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
    FormatConversionDialog dialog(selectedFiles, this);
    dialog.exec();

    // 转换完成后刷新视图
    on_refresh_clicked();
}
