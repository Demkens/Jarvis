import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 独立详情窗口：元数据 + 包内文件结构化预览。
// 数据源：typeEngine.readFields(id) 取元数据；createPackageView(id) 取包内文件清单。
Window {
    id: window
    visible: false
    title: "详情" + (window.detailTitle ? " - " + window.detailTitle : "")
    width: 940
    height: 680
    minimumWidth: 720
    minimumHeight: 520

    property int packageId: 0
    property var detail: ({})
    property var fileList: []
    property string detailTitle: ""

    function formatBytes(bytes) {
        if (!bytes)
            return "0 B"
        if (bytes < 1024)
            return bytes + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        return (bytes / 1024 / 1024).toFixed(2) + " MB"
    }

    function formatTime(iso) {
        if (!iso)
            return "—"
        var s = String(iso)
        return s.length >= 10 ? s.slice(0, 10) + " " + s.slice(11, 16) : s
    }

    function formDisplayName(form) {
        if (!form)
            return "—"
        var list = typeEngine.enabledTypes
        for (var i = 0; i < list.length; ++i) {
            if (list[i].form === form)
                return list[i].displayName
        }
        return form
    }

    function openPackage(id) {
        window.packageId = id
        window.detail = typeEngine.readFields(id)
        window.detailTitle = window.detail.title || "untitled"
        var view = typeEngine.createPackageView(id)
        window.fileList = view ? view.files : []
        window.show()
        window.requestActivate()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 左：封面 + 元数据 ----
        Rectangle {
            Layout.preferredWidth: 360
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "元数据"
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Rectangle {
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 26
                        radius: 4
                        color: "#e6eaf2"
                        Text {
                            anchors.centerIn: parent
                            text: "关闭"
                            color: "#2b2b2b"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: window.close()
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 250
                    Layout.preferredHeight: 180
                    Layout.alignment: Qt.AlignHCenter
                    radius: 6
                    clip: true
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: window.detail.coverUrl || ""
                    }
                }

                Text { text: "标题"; font.pixelSize: 11 }
                Text { text: window.detail.title || "untitled"; font.pixelSize: 14; font.bold: true; wrapMode: Text.WrapAnywhere }

                Text { text: "类型：" + window.formDisplayName(window.detail.type_form); font.pixelSize: 12 }
                Text { text: "创作者：" + (window.detail.creatorName || "佚名")
                       + (window.detail.creatorCount > 1 ? " 等 " + window.detail.creatorCount + " 人" : "")
                       ; font.pixelSize: 12 }
                Text { text: "创作日期：" + (window.detail.creation_date || "—"); font.pixelSize: 12 }
                Text { text: "评分：" + (window.detail.score > 0 ? window.detail.score : "未评分"); font.pixelSize: 12 }
                Text { text: "大小：" + window.formatBytes(window.detail.size); font.pixelSize: 12 }
                Text { text: "入库：" + window.formatTime(window.detail.created_time); font.pixelSize: 12 }
                Text { text: "更新：" + window.formatTime(window.detail.updated_time); font.pixelSize: 12 }

                Item { Layout.fillHeight: true }
            }
        }

        // ---- 右：包内文件清单 ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.topMargin: 14
                Layout.bottomMargin: 8
                text: "包内文件（" + window.fileList.length + " 项）"
                font.pixelSize: 14
                font.bold: true
            }

            GridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                clip: true
                model: window.fileList
                cellWidth: 190
                cellHeight: 150
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    width: 176
                    height: 140
                    radius: 6
                    clip: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4
                        Rectangle {
                            Layout.preferredWidth: parent.width
                            Layout.preferredHeight: 84
                            radius: 4
                            clip: true
                            Image {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectFit
                                source: modelData.url || ""
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || ""
                            elide: Text.ElideRight
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "第 " + modelData.index + " 页 · " + window.formatBytes(modelData.size)
                            elide: Text.ElideRight
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
