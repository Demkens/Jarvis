// 通用菜单行(文件面板等弹出菜单中的一行动作)
import QtQuick

Rectangle {
    id: row

    // 输入: 行标题、行是否可用（离线时置灰导入行）
    property string text: ""
    property bool rowEnabled: true

    // 输出: 鼠标悬停（可用时）、鼠标点击
    signal hovered()
    signal triggered()

    width: parent.width
    height: 30
    radius: 3
    color: !row.rowEnabled ? "transparent" : (rowMouse.containsMouse ? "#9aa3b3" : "transparent")

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 10
        text: row.text
        color: row.rowEnabled ? "#1e2430" : "#8a92a0"
        font.pixelSize: 13
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        onEntered: if (row.rowEnabled) row.hovered()
        onClicked: if (row.rowEnabled) row.triggered()
    }
}
