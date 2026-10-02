// 左栏导航（开发文档 8.1 三键）：资源显示 / 数据统计 / 短笔记。
// 当前仅「资源显示」已实装（常驻高亮），其余为灰色占位，无交互。
// 提示链路已移除（原占位点击提示不再保留）。
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: leftBar
    implicitWidth: 128
    Layout.fillHeight: true
    color: "#12151f"

    // 单个菜单项：高亮态 / 灰色占位态
    component NavItem: Rectangle {
        id: nav
        property string label: ""
        property bool active: false     // 当前选中
        property bool available: true   // 功能是否已实现
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        color: nav.active ? "#1e2634" : "transparent"
        Rectangle {
            width: nav.active ? 3 : 0
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: "#5b9df0"
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: nav.label
            color: nav.active ? "#e6eaf2"
                              : (nav.available ? "#aab3c5" : "#5c6472")
            font.pixelSize: 14
            font.bold: nav.active
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 10
        spacing: 2

        NavItem {
            label: "资源显示"
            active: true
            available: true
        }
        NavItem {
            label: "数据统计"
            available: false
        }
        NavItem {
            label: "短笔记"
            available: false
        }
    }
}