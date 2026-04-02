# 主界面程序入口
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls

import "modules"

import Model 1.0

ApplicationWindow {
    id: root
    width: 1360; height: 865
    minimumWidth: 1300; minimumHeight: 800
    title: "Jarvis" + (dbModel.currentDbName ? " - " + dbModel.currentDbName : "")
    color: "#e6e6e6"
    visible: true

    // 数据库是否已打开
    property bool hasDbOpened: dbModel.currentDbName !== ""

    // 全局数据库模型
    DatabaseModel {id: dbModel}

    // 动态加载器 - 仅声明，不设置source，按需创建
    Loader {id: createDbLoader}
    Loader {id: labelEditorLoader}
    Loader {id: settingEditorLoader}

    // 顶部菜单栏
    menuBar: MenuBar {
        Menu {
            title: "文件"
            MenuItem {
                text: "新建库..."
                // 点击时动态创建Loader，弹窗关闭后销毁
                onTriggered: {
                    var loader = createDbLoader;
                    loader.source = "qrc:/qml/dialogs/CreateDbDialog.qml";
                    loader.item.open();
                    loader.item.closed.connect(function() {
                        loader.source = "";
                    });
                }
            }
            MenuItem {
                text: "切换库..."
                onTriggered: console.log("切换库")
            }
            MenuItem {
                text: "删除库..."
                onTriggered: console.log("删除库")
            }
        }
        Menu {
            title: "标签"
            MenuItem {
                text: "标签管理..."
                onTriggered: {
                    var loader = labelEditorLoader;
                    loader.source = "qrc:/qml/dialogs/LabelDialog.qml";
                    loader.item.open();
                    loader.item.closed.connect(function() {
                        loader.source = "";
                    });
                }
            }
        }
        Menu {
            title: "设置"
            MenuItem {
                text: "偏好设置..."
                onTriggered: {
                    var loader = settingEditorLoader;
                    loader.source = "qrc:/qml/dialogs/SettingDialog.qml";
                    loader.item.open();
                    loader.item.closed.connect(function() {
                        loader.source = "";
                    });
                }
            }
        }
    }

    // 主界面布局
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 左侧导航栏
        ButtonBar {
            onCurrentIndexChanged: pageStack.currentIndex = currentIndex
        }

        // 右侧页面区域
        StackLayout {
            id: pageStack
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 文件浏览页面
            Loader {
                id: homepage
                source: "qrc:/qml/pages/FileBrowser.qml"
            }

            // 文件编辑页面
            Loader {
                id: filepage
                source: hasDbOpened ? "qrc:/qml/pages/FileEditor.qml" : "qrc:/qml/pages/DefaultPage.qml"
            }

            // 数据统计页面
            Loader {
                id: datapage
                source: hasDbOpened ? "qrc:/qml/pages/DataCounter.qml" : "qrc:/qml/pages/DefaultPage.qml"
            }

            // 文档撰写页面
            Loader {
                id: docpage
                source: hasDbOpened ? "qrc:/qml/pages/DocEditor.qml" : "qrc:/qml/pages/DefaultPage.qml"
            }
        }
    }
}
