// 数字卡片：统计页顶部的单个规模指标（大号数字 + 说明标题）。
// 纯展示组件，无数据来源，由调用方注入文本。
import QtQuick

Rectangle {
    id: card

    // 输入: 指标标题（如「数据包总数」）
    property string title: ""
    // 输入: 指标数值文本（已格式化，如 "128"）
    property string value: ""
    // 输入: 标题下方补充说明（可空）
    property string hint: ""

    implicitHeight: 78
    radius: 6
    color: "#f7f9fc"
    border.width: 1
    border.color: "#d5dbe6"

    Column {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.right: parent.right
        anchors.rightMargin: 14
        spacing: 4

        Text {
            text: card.value
            font.pixelSize: 26
            font.bold: true
            color: "#2b2b2b"
        }
        Text {
            text: card.title
            font.pixelSize: 12
            color: "#5c6472"
        }
        Text {
            width: parent.width
            visible: card.hint !== ""
            text: card.hint
            font.pixelSize: 10
            color: "#9aa3b2"
            elide: Text.ElideRight
        }
    }
}