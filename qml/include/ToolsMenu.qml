// 工具菜单（顶栏功能区子组件；与 FileMenu 同构的拆分组件）。
// 职责：集中承载与具体资源无关的工具类入口，避免 TopBar.qml 随功能增长而臃肿。
// 对外信号：tagManagerRequested() —— 选择了「标签管理…」。
import QtQuick
import QtQuick.Controls

import "."

Item {
    id: toolsMenu

    implicitWidth: toolsArea.implicitWidth
    implicitHeight: toolsArea.height

    // 信号: 打开标签管理弹窗
    signal tagManagerRequested()

    Rectangle {
        id: toolsArea
        implicitWidth: toolsLabel.implicitWidth + 20
        height: 26
        radius: 4
        color: toolsAreaMouse.containsMouse || menu.opened ? '#b7cbff' : "transparent"

        Text {
            id: toolsLabel
            anchors.centerIn: parent
            text: "工具"
            font.pixelSize: 13
        }

        MouseArea {
            id: toolsAreaMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: menu.open()
        }

        Menu {
            id: menu
            y: toolsArea.height
            width: 160
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            MenuItem {
                text: "标签管理…"
                // 标签全库共享，须先打开库
                enabled: libraryService.currentName !== ""
                onTriggered: toolsMenu.tagManagerRequested()
            }
        }
    }
}
