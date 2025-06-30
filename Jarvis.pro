QT       += core gui
QT       += concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    src/main.cpp \
    src/gui/widget.cpp \
    src/pages/datacounter.cpp \
    src/pages/filebrowser.cpp \
    src/pages/fileeditor.cpp \
    src/pages/labeleditor.cpp \
    src/pages/settingeditor.cpp \
    src/widgets/infobrowser.cpp \
    src/dialogs/mediabrowser.cpp \
    src/dialogs/formatconversion.cpp

HEADERS += \
    src/gui/widget.h \
    src/pages/datacounter.h \
    src/pages/filebrowser.h \
    src/pages/fileeditor.h \
    src/pages/labeleditor.h \
    src/pages/settingeditor.h \
    src/widgets/infobrowser.h \
    src/dialogs/mediabrowser.h \
    src/dialogs/formatconversion.h

FORMS += \
    src/ui/widget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    rec.qrc

INCLUDEPATH += \
    src/gui \
    src/pages \
    src/widgets \
    src/dialogs \
    src/ui

# src/gui  主界面相关
# src/pages  主要页面
# src/widgets  次级页面
# src/dialogs  对话框
# src/ui  UI文件

# resources/icons  资源文件目录-图标
# resources/styles  资源文件目录-风格
# resources/datas  资源文件目录-数据库
