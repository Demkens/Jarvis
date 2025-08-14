import QtQuick 2.15
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // 分页控制属性
    property int currentPage: 0
    property int itemsPerPage: 100
    property int totalItems: 500
    readonly property int totalPages: Math.ceil(totalItems / itemsPerPage)

    // 当前页显示的数据
    property var currentPageItems: {
        let items = [];
        let startIndex = currentPage * itemsPerPage;
        let endIndex = Math.min(startIndex + itemsPerPage, totalItems);
        for (let i = startIndex; i < endIndex; i++) {
            items.push(i);
        }
        return items;
    }


    // GridView 组件
    GridView {
        id: gridView
        anchors.fill: parent

        // 动态计算单元格尺寸
        cellWidth: Math.floor(width / 10)
        cellHeight: 100

        // 绑定到当前页的数据
        model: currentPageItems

        // 委托项
        delegate: Rectangle {
            width: gridView.cellWidth - 5
            height: gridView.cellHeight - 5
            color: index % 2 === 0 ? "#3498db" : "#2ecc71"

            Text {
                anchors.centerIn: parent
                text: modelData + 1
                font.pixelSize: 14
                color: "white"
            }
        }

        // 页脚组件（翻页按钮）
        footer: Item {
            width: gridView.width
            height: 70

            Rectangle {
                id: pageButton
                width: 200
                height: 50
                anchors.centerIn: parent
                radius: 10
                color: "#3498db"

                Text {
                    anchors.centerIn: parent
                    text: "下一页 (" + (root.currentPage + 1) + "/" + root.totalPages + ")"
                    color: "white"
                    font.bold: true
                    font.pixelSize: 16
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (root.currentPage < root.totalPages - 1) {
                            root.currentPage++
                        }
                    }
                }
            }
        }
    }
}
