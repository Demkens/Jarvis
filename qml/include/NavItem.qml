// 左栏导航项：左侧选中指示条 + 图标按钮（复用 WindowButton 的悬停提亮/点击）。
import QtQuick
import QtQuick.Layouts
import "."

Rectangle {
    id: navItem

    // 输入: 图标资源路径（qrc 绝对路径）
    property string iconSource: ""
    // 是否当前选中（控制左侧指示条与背景高亮）
    property bool active: false
    // 功能是否已实现（未实现则图标置灰）
    property bool available: true
    // 输出: 点击后触发，由调用方（LeftBar）绑定跳转/切换行为
    signal clicked()

    Layout.fillWidth: true
    Layout.preferredHeight: 44
    color: navItem.active ? '#86b2ff' : "transparent"

    // 左侧选中指示条
    Rectangle {
        width: navItem.active ? 3 : 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: "#5b9df0"
    }

    // 图标按钮：复用 WindowButton，悬停提亮仅作用于图标自身区域
    WindowButton {
        width: 40
        height: 40
        anchors.centerIn: parent
        iconSource: navItem.iconSource
        opacity: navItem.available ? 1.0 : 0.35
        onClicked: navItem.clicked()
    }
}