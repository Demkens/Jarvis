# 主页 - 文件浏览页面
import QtQuick 2.15
import QtQuick.Layouts
import QtQuick.Controls

import "../modules"

Rectangle {
    id: root

    GridView {
        id: gridView
        anchors.fill: parent

        cellWidth: 100
        cellHeight: 100 + 47

        // 绑定到当前页的数据
        model: 50

        // 委托项
        delegate: FileDisplayBlock {
            width: gridView.cellWidth
            height: gridView.cellHeight
        }
    }

    FlipPageBox {
        id: flipPageBox
        anchors.bottom: parent.bottom
    }
}
