# 主页-文件浏览容器
import QtQuick

Item {
    id: root

    property real padding: 3                // 内边距属性
    property string imageSource: ""         // 图片源地址
    property real imageAspectRatio: 1.8     // 图片宽高比属性

    Item {
        id: contentArea
        anchors.fill: parent
        anchors.margins: parent.padding

        // 缩略图容器
        Rectangle {
            id: thumbnailContainer
            width: parent.width; height: parent.width
            anchors.top: parent.top
            color: "#f0f0f0"

            // 图片显示组件
            Image {
                id: picDisplay
                anchors.centerIn: parent

                width: {
                    if (root.imageAspectRatio > 1) return parent.width
                    else return parent.height * root.imageAspectRatio
                }

                height: {
                    if (root.imageAspectRatio > 1) return parent.width / root.imageAspectRatio
                    else return parent.height
                }

                source: root.imageSource
                fillMode: Image.PreserveAspectFit               // 设置图片均匀缩放以适应
                sourceSize.width: 400; sourceSize.height: 400   // 设置加载尺寸以提高性能
            }

            // 默认占位图
            Rectangle {
                anchors.fill: parent
                color: "#f0f0f0"
                visible: !picDisplay.source || picDisplay.status !== Image.Ready

                Text {
                    anchors.centerIn: parent
                    text: "待上传..."
                    color: "white"
                    font.pixelSize: 15
                    font.bold: true
                }
            }
        }

        // 文件名栏
        Text {
            id: dataText
            width: parent.width;
            anchors.top: thumbnailContainer.bottom
            anchors.topMargin: 2
            text: "文件名"
            elide: Text.ElideRight
            font.pixelSize: 12
            font.bold: true
        }

        // 作者名栏
        Text {
            id: nameText
            width: parent.width;
            anchors.top: dataText.bottom
            text: "作者名"
            elide: Text.ElideRight
            font.pixelSize: 11
            color: "#666"
        }
    }
}
