// 设置弹窗
import QtQuick 2.15
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    width: 500; height: 400
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            text: qsTr("偏好设置")
            font.pixelSize: 20
            font.bold: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                spacing: 15

                GroupBox {
                    title: "界面"
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 10

                        RowLayout {
                            Label { text: "主题" }
                            ComboBox {
                                model: ["浅色", "深色", "跟随系统"]
                            }
                        }

                        RowLayout {
                            Label { text: "缩放比例" }
                            Slider {
                                from: 0.8
                                to: 1.5
                                value: 1.0
                            }
                        }
                    }
                }

                GroupBox {
                    title: "文件浏览"
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 10

                        RowLayout {
                            Label { text: "每页数量" }
                            SpinBox {
                                from: 10
                                to: 100
                                value: 50
                            }
                        }

                        RowLayout {
                            Label { text: "缩略图大小" }
                            Slider {
                                from: 80
                                to: 150
                                value: 100
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Item { Layout.fillWidth: true }
            Button {
                text: "保存"
                highlighted: true
                onClicked: root.close()
            }
            Button {
                text: "取消"
                onClicked: root.close()
            }
        }
    }
}

