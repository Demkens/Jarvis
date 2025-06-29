#include "widget.h"
#include "ui_widget.h"

Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget)
{
    this->setWindowTitle("Javris");
    this->resize(1360, 865);
    this->setMinimumSize(1300, 800);
    this->setStyleSheet("background-color: #e6e6e6;");

    setupUI();
    setupConnections();
}

Widget::~Widget()
{
    delete ui;
}

void Widget::setupUI()
{
    // 创建控件模型（从高级到低级）
    hlayout = new QHBoxLayout();          // 主布局

    vlayout = new QVBoxLayout();          // 0级布局
    btn_home = new QPushButton(this);     // 1级组件
    btn_file = new QPushButton(this);     // 1级组件
    btn_set = new QPushButton(this);      // 1级组件

    stack = new QStackedWidget(this);     // 0级组件
    page_home = new QWidget(stack);       // 1级窗口
    page_manage = new ManagePage(stack);  // 1级窗口
    page_set = new QWidget(stack);        // 1级窗口

    // 配置控件属性（集中配置）
    btn_home->setIcon(QIcon(":/btn_icon/resources/icons/home.svg"));
    btn_file->setIcon(QIcon(":/btn_icon/resources/icons/file.svg"));
    btn_set->setIcon(QIcon(":/btn_icon/resources/icons/set.svg"));

    btn_home->setIconSize(QSize(32, 32));
    btn_file->setIconSize(QSize(32, 32));
    btn_set->setIconSize(QSize(32, 32));

    btn_home->setFlat(true);
    btn_file->setFlat(true);
    btn_set->setFlat(true);

    ui->setupUi(page_home);
    ui->setupUi(page_set);

    page_home->findChildren<QLabel *>().at(0)->setText("主页");
    page_set->findChildren<QLabel *>().at(0)->setText("设置");

    page_home->setStyleSheet("background-color: #fdfdfd;");
    page_manage->setStyleSheet("background-color: #fdfdfd;");
    page_set->setStyleSheet("background-color: #fdfdfd;");

    stack->setStyleSheet(R"(QStackedWidget {background-color: #fdfdfd;})");

    // 构建布局层次（从低级到高级）
    vlayout->addStretch(1);
    vlayout->addWidget(btn_home);
    vlayout->addStretch(1);
    vlayout->addWidget(btn_file);
    vlayout->addStretch(40);
    vlayout->addWidget(btn_set);
    vlayout->addStretch(1);

    stack->addWidget(page_home);
    stack->addWidget(page_manage);
    stack->addWidget(page_set);

    hlayout->addLayout(vlayout);
    hlayout->addWidget(stack);

    // 初始化尺寸
    hlayout->setContentsMargins(8, 0, 0, 0);
    setLayout(hlayout);
}

void Widget::setupConnections()
{
    connect(btn_home, &QPushButton::clicked, [=](){stack->setCurrentIndex(0);});
    connect(btn_file, &QPushButton::clicked, [=](){stack->setCurrentIndex(1);});
    connect(btn_set, &QPushButton::clicked, [=](){stack->setCurrentIndex(2);});
}

