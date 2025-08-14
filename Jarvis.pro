QT += qml quick sql widgets

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    src/bridge/cppobj.cpp \
    src/core/dbmodel.cpp \
    src/main.cpp

HEADERS += \
    src/bridge/cppobj.h \
    src/core/dbmodel.h

RESOURCES += \
    qml.qrc \
    rec.qrc

DISTFILES += \
    qml/dialogs/CreateDbDialog.qml \
    qml/modules/NormalButton.qml \
    qml/pages/DataCounter.qml \
    qml/pages/FileBrowser.qml \
    qml/pages/FileEditor.qml \
    qml/pages/LabelEditor.qml \
    qml/pages/SettingEditor.qml \
    qml/main.qml

INCLUDEPATH += \
    src/core

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH = $$PWD/qml

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH =

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# qml/modules   组件
# qml/pages     页面
# qml/dialogs   弹窗

# src   后端C++实现

# envs  抽象库

# resource/icons  资源文件目录-图标
# resource/styles  资源文件目录-风格
# resource/datas  资源文件目录-数据库


