import QtQuick 2.15
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root

    property var linkedFile: null

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            border.color: "#cccccc"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: "#f5f5f5"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        Label { text: "关联文件" }
                    }
                }

                ListView {
                    id: linkedFileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: []
                    delegate: Item {
                        width: parent ? parent.width : 0
                        height: 40

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            color: ListView.isCurrentItem ? "#e0e0e0" : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                Text {
                                    text: "文件占位"
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: linkedFileList.currentIndex = index
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            border.color: "#cccccc"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: "#f5f5f5"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10

                        Label {
                            text: "文档标题"
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "保存"
                            highlighted: true
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 10

                    TextArea {
                        id: docContent
                        placeholderText: "在此撰写文档内容...\n\n文档可与文件关联，帮助您记录和管理文件的详细信息、使用说明、分类备注等。"
                        font.pixelSize: 14
                        wrapMode: TextArea.Wrap
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: "#f5f5f5"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10

                        Label { text: "关联标签:" }
                        Label { text: "无" }
                        Item { Layout.fillWidth: true }
                        Button { text: "添加关联" }
                    }
                }
            }
        }
    }
}