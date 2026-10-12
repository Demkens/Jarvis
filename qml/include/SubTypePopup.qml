// 类型选择子面板(级联于触发行右侧的 Popup)
import QtQuick
import QtQuick.Controls

Popup {
    id: sub

    // 输入: 当前通道（"wizard"/"preprocess"），open 前由调用方设置
    property string channel: ""

    // 输出: 选定了类型 form
    signal typeSelected(string form)

    parent: Overlay.overlay
    width: 96
    padding: 6
    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

    background: Rectangle {
        radius: 4
        color: '#b7bac0'
        border.width: 1
        border.color: '#a2a9b5'
    }

    contentItem: Column {
        spacing: 2

        Repeater {
            model: typeEngine.enabledTypes
            delegate: Rectangle {
                required property var modelData
                width: sub.width - 12   // 扣除 padding 6*2
                height: 30
                radius: 3
                color: typeItemMouse.containsMouse ? "#9aa3b3" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: modelData.displayName
                    color: "#1e2430"
                    font.pixelSize: 13
                }

                MouseArea {
                    id: typeItemMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: sub.typeSelected(modelData.form)
                }
            }
        }
    }
}
