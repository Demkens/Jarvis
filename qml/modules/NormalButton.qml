import QtQuick

Item {
    id: root
    width: 35; height: 35

    property alias iconSource: icon.source
    property bool selected: false
    signal clicked()

    // 背景
    Rectangle {
        id: background
        anchors.fill: parent
        radius: 8
        color: "transparent"

        states: [
            State {                     // 悬停/选中状态
                name: "selected"
                when: root.selected || mouseArea.containsMouse
                PropertyChanges {
                    target: background
                    color: "#CFCFCF"
                }
            },

            State {                     // 未接触状态
                name: "unselect"
                when: !root.selected
                PropertyChanges {
                    target: background
                    color: "transparent"
                }
            }
        ]
    }

    // 图标
    Image {
        id: icon
        anchors.centerIn: parent
        width: 20; height: 20
        //sourceSize.width: 50; sourceSize.height: 50
        smooth: true          // 启用抗锯齿
        mipmap: true          // 启用mipmap优化缩放
        antialiasing: true    // 启用抗锯齿渲染
    }

    // 鼠标区域
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        onClicked: {
            if (!root.selected) {
                root.selected = true
                root.clicked()
            }
        }
    }
}
