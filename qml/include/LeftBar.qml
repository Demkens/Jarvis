// 左侧导航栏：资源显示 + 数据统计 + 短笔记。
import QtQuick
import QtQuick.Layouts

import "."

Rectangle {
    id: leftBar
    implicitWidth: 50
    Layout.fillHeight: true
    color: '#333e60'

    // 输入: 当前选中页索引（外部传入，作为各导航项高亮的唯一依据）
    property int currentIndex: 0
    // 输出: 点击某导航项时发出，携带目标页索引
    signal pageSelected(int index)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 10
        spacing: 2

        NavItem {
            iconSource: "qrc:/res/title/file.png"
            active: leftBar.currentIndex === 0
            available: true
            onClicked: leftBar.pageSelected(0)
        }
        NavItem {
            iconSource: "qrc:/res/title/data.png"
            active: leftBar.currentIndex === 1
            available: true
            onClicked: leftBar.pageSelected(1)
        }
        NavItem {
            iconSource: "qrc:/res/title/note.png"
            active: leftBar.currentIndex === 2
            available: true
            onClicked: leftBar.pageSelected(2)
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}