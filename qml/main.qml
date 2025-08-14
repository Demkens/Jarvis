import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls

import "modules"
import "pages"
import "dialogs"

import Model 1.0

ApplicationWindow {
    id: root
    width: 1360; height: 865
    minimumWidth: 1300; minimumHeight: 800
    title: "Javris" + " - " + dbModel.currentDbName
    color: "#e6e6e6"
    visible: true

    // 全局数据库模型
    DatabaseModel {id: dbModel}

    // 动态加载对象
    Loader {
        id: createDbLoader
        property DatabaseModel dbModelalies : dbModel
    }

    // 顶部菜单栏
    menuBar: MenuBar {
        Menu {
            title: "文件"
            MenuItem {
                text: "新建库..."
                onTriggered: {
                    createDbLoader.source = "qrc:/qml/dialogs/CreateDbDialog.qml";
                    createDbLoader.item.open();
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
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 左侧导航栏 (垂直布局)
        ColumnLayout {
            Layout.preferredWidth: 45   // 导航栏宽度
            Layout.fillHeight: true

            // 空白填充项
            Item {Layout.preferredHeight: 5}

            // 主页按钮
            NormalButton {
                id: home
                iconSource: "qrc:/resource/icons/home.svg"
                Layout.alignment: Qt.AlignCenter
                onClicked: {
                    file.selected = false
                    setting.selected = false
                    pageStack.currentIndex = 0
                }
            }

            // 文件按钮
            NormalButton {
                id: file
                iconSource: "qrc:/resource/icons/file.svg"
                Layout.alignment: Qt.AlignCenter
                onClicked: {
                    home.selected = false
                    setting.selected = false
                    pageStack.currentIndex = 1
                }
            }

            // 空白填充项
            Item {Layout.fillHeight: true}

            // 设置按钮
            NormalButton {
                id: setting
                iconSource: "qrc:/resource/icons/set.svg"
                Layout.alignment: Qt.AlignCenter
                onClicked: {
                    home.selected = false
                    file.selected = false
                    pageStack.currentIndex = 2
                }
            }

            // 空白填充项
            Item {Layout.preferredHeight: 10}
        }

        // 右侧页面区域
        StackLayout {
            id: pageStack
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 各页面内容...
            FileBrowser {
                id: page1
            }

            FileEditor {
                id: page2
            }

            SettingEditor {
                id: page3
            }
        }
    }
}
