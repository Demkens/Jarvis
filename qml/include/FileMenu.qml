// 文件菜单
import QtQuick
import QtQuick.Controls

import "."

Item {
    id: fileMenu

    implicitWidth: fileArea.implicitWidth
    implicitHeight: fileArea.height

    // 信号: 选定了导入类型（form 与通道由调用方决定怎么用）
    signal fileImportRequested(string form, string channel)
    // 信号: 菜单内"刷新页面"被触发
    signal refreshRequested()

    // 文件菜单触发按钮
    Rectangle {
        id: fileArea
        implicitWidth: fileLabel.implicitWidth + 20
        height: 26
        radius: 4
        color: fileAreaMouse.containsMouse || menu.opened ? '#525c75' : "transparent"

        Text {
            id: fileLabel
            anchors.centerIn: parent
            text: "文件"
            font.pixelSize: 13
        }

        MouseArea {
            id: fileAreaMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: menu.open()
        }

        // 文件菜单
        Menu {
            id: menu
            y: fileArea.height
            width: 160
            cascade: true
            // 标准关闭策略：Esc 或按下菜单外任意位置（含顶栏）关闭——即"先关菜单再拖动"
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            // 子菜单: 导入数据（动态类型项经 Instantiator 注入）
            Menu {
                id: importMenu
                title: "导入数据"
                enabled: importService.isOnline()

                Instantiator {
                    id: importInstantiator
                    model: typeEngine.enabledTypes
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.displayName
                        onTriggered: fileMenu.fileImportRequested(modelData.form, "wizard")
                    }
                    onObjectAdded: (index, object) => importMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => importMenu.removeItem(object)
                }
            }

            // 子菜单: 导入预处理数据
            Menu {
                id: preprocessMenu
                title: "导入预处理数据"
                enabled: importService.isOnline()

                Instantiator {
                    id: preprocessInstantiator
                    model: typeEngine.enabledTypes
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.displayName
                        onTriggered: fileMenu.fileImportRequested(modelData.form, "preprocess")
                    }
                    onObjectAdded: (index, object) => preprocessMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => preprocessMenu.removeItem(object)
                }
            }

            MenuSeparator {}

            // 静态命令
            Action {
                text: "刷新页面"
                onTriggered: fileMenu.refreshRequested()
            }
        }
    }
}