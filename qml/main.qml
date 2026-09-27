// 主窗口：顶部切换「资源」（数据包列表/导入/导出/查看）与「库管理」（M1 仪表盘）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "dialogs"
import "windows"

ApplicationWindow {
    id: root
    width: 1360; height: 865
    minimumWidth: 1300; minimumHeight: 800
    visible: true
    title: "Jarvis" + (libraryService.currentName ? " - " + libraryService.currentName : "")
    color: "#e6e6e6"

    // true=资源页，false=库管理页；无打开库时强制管理页
    property bool resourcePage: true
    readonly property bool libraryOpen: libraryService.currentName !== ""

    // 操作结果提示（3.5 秒后淡出）
    function showStatus(message, isError) {
        statusText.text = message
        statusText.color = isError ? "#c0341d" : "#2d7a2d"
        statusTimer.restart()
    }

    // 字节数人类可读
    function humanSize(bytes) {
        if (bytes >= 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
        if (bytes >= 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        if (bytes >= 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        return bytes + " B"
    }

    header: Rectangle {
        height: 52
        color: "#d8d8d8"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            Text {
                text: "当前库：" + (libraryService.currentName || "（未打开）")
                font.pixelSize: 15
                font.bold: true
            }

            // 页面切换（库打开后可用）
            Row {
                spacing: 4
                Button {
                    text: "资源"
                    flat: true
                    highlighted: root.resourcePage
                    enabled: root.libraryOpen
                    onClicked: root.resourcePage = true
                }
                Button {
                    text: "库管理"
                    flat: true
                    highlighted: !root.resourcePage
                    onClicked: root.resourcePage = false
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                visible: root.resourcePage
                text: "导入数据包"
                highlighted: true
                enabled: root.libraryOpen
                onClicked: importDialog.open()
            }
            Button {
                visible: !root.resourcePage
                text: "新建库"
                highlighted: true
                onClicked: createDialog.open()
            }
        }
    }

    // ========== 资源页 ==========
    ColumnLayout {
        visible: root.resourcePage && root.libraryOpen
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: typeEngine.packages.length + " 个数据包"
                font.pixelSize: 14
                color: "#555555"
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: !importService.isOnline()
                text: "实体链接库离线：可浏览封面与元数据，图片查看/导入/导出不可用"
                font.pixelSize: 12
                color: "#c0341d"
            }
            Button {
                text: "刷新"
                onClicked: typeEngine.refresh()
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: typeEngine.packages

            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 110
                radius: 4
                color: "#f5f5f5"
                border.width: 1
                border.color: "#cfcfcf"

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onDoubleClicked: viewerWindow.openPackage(modelData.id)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 14

                    Rectangle {
                        Layout.preferredWidth: 74
                        Layout.preferredHeight: 90
                        radius: 3
                        color: "#e2e2e2"
                        clip: true
                        border.width: 1
                        border.color: "#c8c8c8"
                        Image {
                            anchors.fill: parent
                            source: modelData.coverUrl
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: modelData.title
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Text {
                            text: (modelData.creatorName || "anonymous")
                                  + "    类型：" + modelData.type_form
                            color: "#555555"
                            font.pixelSize: 12
                        }
                        Text {
                            text: "评分 " + Math.round(modelData.score * 100)
                                  + "    占用 " + root.humanSize(modelData.size)
                            color: "#777777"
                            font.pixelSize: 12
                        }
                    }

                    Button {
                        text: "查看"
                        onClicked: viewerWindow.openPackage(modelData.id)
                    }
                    Button {
                        text: "导出"
                        enabled: importService.isOnline()
                        onClicked: {
                            exportTargetId = modelData.id
                            exportFolderPicker.open()
                        }
                    }
                }
            }
        }

        Text {
            id: statusText
            visible: text !== ""
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    // ========== 库管理页（M1 仪表盘）==========
    ColumnLayout {
        visible: !root.resourcePage || !root.libraryOpen
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Text {
            visible: libraryService.libraries.length === 0
            text: "尚未创建任何库。点击右上角「新建库」开始：\n"
                + "数据集（元数据、封面）存于软件内 envs 目录，实体文件存于你指定的链接库目录。"
            color: "#666666"
            font.pixelSize: 14
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: libraryService.libraries

            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 86
                radius: 4
                color: modelData.isCurrent ? "#cfe2f3" : "#f5f5f5"
                border.width: 1
                border.color: "#cfcfcf"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: (modelData.isCurrent ? "● " : "○ ") + modelData.name
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Text {
                            text: "链接库：" + modelData.linkAddress
                                  + "    类型：" + (modelData.support.length
                                      ? modelData.support.join(", ") : "（未启用）")
                            color: "#555555"
                            font.pixelSize: 12
                        }
                    }

                    Button {
                        text: "切换"
                        enabled: !modelData.isCurrent
                        onClicked: {
                            const result = libraryService.switchLibrary(modelData.name)
                            if (!result.ok)
                                root.showStatus(result.message, true)
                            else
                                root.resourcePage = true
                        }
                    }
                    Button {
                        text: "删除"
                        onClicked: {
                            deleteDialog.libraryName = modelData.name
                            deleteDialog.linkAddress = modelData.linkAddress
                            deleteDialog.open()
                        }
                    }
                }
            }
        }

        Text {
            visible: !root.resourcePage && statusText.text !== ""
            text: statusText.text
            font.pixelSize: 13
            color: statusText.color
            wrapMode: Text.WordWrap
        }
    }

    Timer {
        id: statusTimer
        interval: 3500
        onTriggered: statusText.text = ""
    }

    CreateLibraryDialog {
        id: createDialog
        // 注意：libraryService 是 QML 上下文属性而非本窗口属性，
        // 必须用裸标识符（沿作用域链查找），写 root.libraryService 会得到 undefined。
        libraryService: libraryService
        typePackageManager: typePackageManager
        onCompleted: function (name) {
            root.showStatus("库「" + name + "」创建完成并已切换", false)
            root.resourcePage = true
        }
    }

    DeleteLibraryDialog {
        id: deleteDialog
        onConfirmed: function (deleteFiles) {
            const result = libraryService.deleteLibrary(libraryName, deleteFiles)
            if (result.ok)
                root.showStatus("库「" + libraryName + "」已删除"
                                + (deleteFiles ? "（含实体文件）" : "（实体文件已保留）"), false)
            else
                root.showStatus(result.message, true)
        }
    }

    ImportDialog {
        id: importDialog
    }

    ViewerWindow {
        id: viewerWindow
    }

    // 导出目标：选择空文件夹后原样复原包文件（开发文档 5.6）
    property int exportTargetId: 0
    FolderDialog {
        id: exportFolderPicker
        title: "导出到空文件夹"
        onAccepted: {
            var local = String(selectedFolder)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            const result = importService.exportPackage(root.exportTargetId, local)
            if (result.ok)
                root.showStatus("已导出 " + result.count + " 个文件到：" + local, false)
            else
                root.showStatus(result.message, true)
        }
    }
}
