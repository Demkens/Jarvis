# 主页-翻页按钮栏，整体呈三个方形框横向排列的形式，左右按钮点击后会发送上翻和下翻信号，中间显示当前页码和总页码
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property int currentPage: 0                 // 当前页
    property int totalPages: 1                  // 总页数
    property int pageDisplay: currentPage + 1   // 显示给用户的页码（从1开始）

    property int buttonSize: 40                 // 方形按钮的尺寸
    property int spacing: 10                    // 按钮间的间距

    property color enabledColor: "#0078d4"      // 按钮可用时的颜色
    property color disabledColor: "#cccccc"     // 按钮不可用时的颜色
    property color textColor: "white"           // 按钮文字的颜色
    property color pageButtonColor: "#f0f0f0"
    property color pageButtonTextColor: "black"

    signal prevPage()       // 上翻页信号
    signal nextPage()       // 下翻页信号
    signal pageClicked()

    width: parent.width; height: buttonSize + 10

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: root.spacing

        // 上一页按钮
        Rectangle {
            id: prevButton
            width: root.buttonSize; height: root.buttonSize
            radius: 4
            color: prevButton.enabled ? root.enabledColor : root.disabledColor
            enabled: root.currentPage > 0

            Text {
                id: prevBtnText
                anchors.centerIn: parent
                text: "<<"
                font.pixelSize: 20
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.previousPage()
                }
            }
        }

        // 页面显示按钮
        Button {
            id: pageButton
            Layout.fillWidth: true
            Layout.preferredHeight: root.buttonSize

            text: `${root.pageDisplay} / ${root.totalPages}`
            font.pixelSize: 14
            font.bold: true

            // 样式
            background: Rectangle {
                color: root.pageButtonColor
                radius: 4
                border.color: "#cccccc"
                border.width: 1
            }

            contentItem: Text {
                text: pageButton.text
                font: pageButton.font
                color: root.pageButtonTextColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            // 点击事件
            onClicked: {
                root.pageClicked()
            }
        }

        // 下一页按钮
        Button {
            id: nextButton
            Layout.preferredWidth: root.buttonSize
            Layout.preferredHeight: root.buttonSize
            font.pixelSize: 20
            font.bold: true

            // 样式
            background: Rectangle {
                color: nextButton.enabled ? root.enabledColor : root.disabledColor
                radius: 4
            }

            contentItem: Text {
                text: ">>"
                font: nextButton.font
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            // 状态
            enabled: root.currentPage < root.totalPages - 1

            // 点击事件
            onClicked: {
                root.nextPage()
            }
        }
    }
}
