// 标签编辑弹窗
import QtQuick 2.15
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: root
    width: 700; height: 500
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            text: qsTr("标签管理")
            font.pixelSize: 20
            font.bold: true
        }

        RowLayout {
            spacing: 10

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "搜索标签..."
            }

            Button {
                text: "搜索"
            }
        }

        RowLayout {
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                border.color: "#cccccc"

                ListView {
                    anchors.fill: parent
                    model: []
                    delegate: Text { text: "标签占位" }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                border.color: "#cccccc"

                ListView {
                    anchors.fill: parent
                    model: []
                    delegate: Text { text: "关系占位" }
                }
            }
        }

        RowLayout {
            spacing: 10

            Button { text: "新增标签" }
            Button { text: "删除标签" }
            Button { text: "编辑标签" }
            Item { Layout.fillWidth: true }
            Button {
                text: "关闭"
                onClicked: root.close()
            }
        }
    }
}

