// 数据包导入对话框

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 840; height: 640
    title: "导入数据"
    modal: true
    standardButtons: Dialog.Close
    padding: 0      // 弹窗内容区与边框间的内边距

    property string form: ""
    property string channel: "wizard"
    property var scanPlans: []
    property var lastResult: ({})

    // 函数: 清空弹窗所有瞬时状态（类型/通道由 start() 传入，此处统一置空）
    function resetDialog() {
        form = ""
        channel = "wizard"
        scanPlans = []
        lastResult = ({})
        scanRoot.text = ""
        scanWarnings.messages = []
        steps.currentIndex = 0
        wizardLoader.active = false
    }

    // 函数: 外部入口——文件菜单选中类型后调用，传入类型与通道并打开，
    //       直接跳到对应页（向导页/预处理页），省略原第1页的类型+通道选择。
    // 输入: selectedForm 类型标识、selectedChannel 通道("wizard"/"preprocess")
    function start(selectedForm, selectedChannel) {
        resetDialog()
        form = selectedForm
        channel = selectedChannel
        if (channel === "wizard") {
            wizardLoader.active = true
            steps.currentIndex = 0
        } else {
            steps.currentIndex = 1
        }
        open()
    }

    // 函数: 导入完成, 存储结果
    function showResult(result) {
        lastResult = result
        steps.currentIndex = 2
    }

    // 背景显示调整
    background: Rectangle {
        radius: 6
        color: '#b5bbc5'         // 面板/浮出区（比 core 略亮）
        border.width: 1
        border.color: "#272f3d"  // 分隔线色作边框
    }

    // 页面切换管理器
    StackLayout {
        id: steps
        anchors.fill: parent
        anchors.margins: 16
        currentIndex: 0

        // ---------- 向导页 ----------
        Loader {
            id: wizardLoader
            active: false
            source: dialog.form !== "" ? typeEngine.wizardUrl(dialog.form) : ""
            onLoaded: {
                item.form = dialog.form
                item.plansSubmitted.connect(function (result) { dialog.showResult(result) })
                item.cancelRequested.connect(function () {
                    wizardLoader.active = false
                    dialog.close()
                })
            }
        }

        // ---------- 预处理扫描 + 确认 ----------
        ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Button {
                    text: "选择待整理目录…"
                    onClicked: rootFolderPicker.open()
                }
                Label {
                    text: scanRoot.text
                    font.pixelSize: 12
                    color: "#aab3c5"
                    elide: Label.ElideMiddle
                    Layout.fillWidth: true
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "取消"
                    onClicked: dialog.close()
                }
                Button {
                    text: "开始导入 " + scanPlans.length + " 个包"
                    highlighted: true
                    enabled: scanPlans.length > 0
                    onClicked: {
                        var plans = []
                        for (var i = 0; i < scanPlans.length; ++i) {
                            var p = scanPlans[i]
                            plans.push({
                                title: p.title,
                                creatorName: p.creatorName,
                                creationDate: p.creationDate,
                                files: p.files,
                                coverSourcePath: p.coverSourcePath
                            })
                        }
                        dialog.showResult(importService.executePlans(dialog.form, plans))
                    }
                }
            }

            // 扫描警告（命名约定未满足的项目）
            Rectangle {
                visible: scanWarnings.count > 0
                Layout.fillWidth: true
                implicitHeight: scanWarnings.implicitHeight + 16
                radius: 4
                color: "#262c38"
                border.width: 1
                border.color: "#a07030"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Repeater {
                        id: scanWarnings
                        property var messages: []
                        model: messages
                        Text {
                            font.pixelSize: 12
                            color: "#d9a05b"
                            text: "⚠ " + (index + 1) + ". " + modelData
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            Label {
                visible: scanPlans.length === 0
                text: "约定：每个子文件夹 = 一个图集包（图片按文件名排序，可放 meta.json 补标题/画家/日期/封面）；根目录散图合并为一个包。"
                color: "#aab3c5"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                visible: scanPlans.length > 0

                Column {
                    width: dialog.width - 48
                    spacing: 8

                    Repeater {
                        model: dialog.scanPlans
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: parent.width
                            implicitHeight: rowLayout.implicitHeight + 20
                            radius: 4
                            color: "#141824"
                            border.width: 1
                            border.color: "#272f3d"

                            RowLayout {
                                id: rowLayout
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    spacing: 2
                                    RowLayout {
                                        spacing: 8
                                        Label { text: "标题"; font.pixelSize: 12; color: "#aab3c5" }
                                        TextField {
                                            text: dialog.scanPlans[index].title
                                            font.pixelSize: 13
                                            color: "#e6eaf2"
                                            Layout.preferredWidth: 240
                                            background: Rectangle { color: "#1e2634"; border.color: "#272f3d"; border.width: 1; radius: 3 }
                                            onTextChanged: dialog.scanPlans[index].title = text
                                        }
                                    }
                                    RowLayout {
                                        spacing: 8
                                        Label { text: "画家"; font.pixelSize: 12; color: "#aab3c5" }
                                        TextField {
                                            text: dialog.scanPlans[index].creatorName
                                            placeholderText: "佚名"
                                            placeholderTextColor: "#5c6472"
                                            font.pixelSize: 13
                                            color: "#e6eaf2"
                                            Layout.preferredWidth: 160
                                            background: Rectangle { color: "#1e2634"; border.color: "#272f3d"; border.width: 1; radius: 3 }
                                            onTextChanged: dialog.scanPlans[index].creatorName = text
                                        }
                                        Label { text: "日期"; font.pixelSize: 12; color: "#aab3c5" }
                                        TextField {
                                            text: dialog.scanPlans[index].creationDate
                                            placeholderText: "YYYY-MM-DD"
                                            placeholderTextColor: "#5c6472"
                                            font.pixelSize: 13
                                            color: "#e6eaf2"
                                            Layout.preferredWidth: 140
                                            background: Rectangle { color: "#1e2634"; border.color: "#272f3d"; border.width: 1; radius: 3 }
                                            onTextChanged: dialog.scanPlans[index].creationDate = text
                                        }
                                    }
                                    Text {
                                        text: dialog.scanPlans[index].files.length + " 个文件"
                                              + (dialog.scanPlans[index].coverSourcePath
                                                 ? "；封面：" + dialog.scanPlans[index].coverSourcePath
                                                     .split(/[\\/]/).pop() : "；封面默认首张")
                                        color: "#aab3c5"
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label { id: scanRoot; visible: false }
        }

        // ---------- 结果报告 ----------
        ColumnLayout {
            spacing: 12

            Item { Layout.preferredHeight: 30 }

            Text {
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 18
                font.bold: true
                color: lastResult.failed > 0 ? "#e5675f" : "#4caf7d"
                text: lastResult.failed > 0
                      ? "导入完成：成功 " + lastResult.success + " 个，失败 " + lastResult.failed + " 个"
                      : "全部 " + (lastResult.success || 0) + " 个数据包导入成功"
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                visible: lastResult.failed > 0

                Column {
                    width: dialog.width - 48
                    spacing: 6
                    Repeater {
                        model: dialog.lastResult.results || []
                        delegate: Rectangle {
                            required property var modelData
                            visible: !modelData.ok
                            width: parent.width
                            height: 34
                            radius: 4
                            color: "#2a2320"
                            Text {
                                anchors.centerIn: parent
                                font.pixelSize: 12
                                color: "#e5675f"
                                text: "第 " + (modelData.index + 1) + " 项失败：" + modelData.message
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "完成"
                    highlighted: true
                    onClicked: {
                        dialog.accept()
                        dialog.resetDialog()
                    }
                }
            }
        }
    }

    // 预处理：选择待整理目录（本地文件夹 URL）
    FolderDialog {
        id: rootFolderPicker
        title: "选择待整理目录"
        onAccepted: {
            var local = String(selectedFolder)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            local = decodeURIComponent(local) // %20 等还原为真实路径
            scanRoot.text = local
            var result = importService.scanPreprocess(dialog.form, local)
            if (!result.ok) {
                scanPlans = []
                scanWarnings.messages = [result.message || "扫描失败"]
                return
            }
            // C++ 结构转成可编辑 JS 副本
            scanPlans = JSON.parse(JSON.stringify(result.plans || []))
            scanWarnings.messages = result.warnings || []
        }
    }
}