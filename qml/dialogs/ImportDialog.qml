// 核心导入对话框（开发文档 5.4 / 7.4）
// 职责：选择类型与导入通道 → 装载类型包向导页 或 预处理确认页 → 调核心执行器 → 结果报告。
// 对话框本身不识别任何具体类型，类型差异由类型包页面和核心扫描规则承担。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog

    title: "导入数据包"
    modal: true
    width: 840
    height: 640
    standardButtons: Dialog.Close
    padding: 0

    // 当前选中的类型 form / 通道
    property string form: ""
    property string channel: "wizard"   // wizard | preprocess
    property var scanPlans: []          // 预处理确认页可编辑的计划副本
    property var lastResult: ({})

    function resetDialog() {
        form = ""
        channel = "wizard"
        scanPlans = []
        lastResult = ({})
        steps.currentIndex = 0
        wizardLoader.active = false
        onlineNote.text = importService.isOnline() ? "" : "当前实体链接库不可达，导入已禁用。"
    }

    function showResult(result) {
        lastResult = result
        steps.currentIndex = 3
    }

    onOpened: resetDialog()

    background: Rectangle {
        radius: 6
        color: "#f0f0f0"
        border.width: 1
        border.color: "#b8b8b8"
    }

    StackLayout {
        id: steps
        anchors.fill: parent
        anchors.margins: 16
        currentIndex: 0

        // ---------- 第 1 步：类型 + 通道 ----------
        ColumnLayout {
            spacing: 14

            Text {
                visible: onlineNote.text !== ""
                text: onlineNote.text
                color: "#c0341d"
                font.pixelSize: 13
            }

            Label { text: "数据包类型"; font.pixelSize: 14; font.bold: true }
            ComboBox {
                id: typeCombo
                Layout.fillWidth: true
                model: typeEngine.enabledTypes
                textRole: "displayName"
                enabled: importService.isOnline()
                onActivated: dialog.form = currentValue.form
                Component.onCompleted: {
                    if (count > 0) {
                        currentIndex = 0
                        dialog.form = currentValue.form
                    }
                }
            }

            Label { text: "导入方式"; font.pixelSize: 14; font.bold: true }
            RadioButton {
                text: "向导导入：手动挑选图片、分组、补元数据后入库"
                checked: dialog.channel === "wizard"
                enabled: importService.isOnline()
                onClicked: dialog.channel = "wizard"
            }
            RadioButton {
                text: "预处理导入：选择整理好的目录，自动识别子文件夹为图集包"
                checked: dialog.channel === "preprocess"
                enabled: importService.isOnline()
                onClicked: dialog.channel = "preprocess"
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "下一步"
                    highlighted: true
                    enabled: form !== "" && importService.isOnline()
                    onClicked: {
                        if (dialog.channel === "wizard") {
                            wizardLoader.active = true
                            steps.currentIndex = 1
                        } else {
                            steps.currentIndex = 2
                        }
                    }
                }
            }

            Text { id: onlineNote; visible: false }
        }

        // ---------- 第 2 步：类型包向导页 ----------
        Loader {
            id: wizardLoader
            active: false
            source: dialog.form !== "" ? typeEngine.wizardUrl(dialog.form) : ""
            onLoaded: {
                item.form = dialog.form
                item.plansSubmitted.connect(function (result) { dialog.showResult(result) })
            }
        }

        // ---------- 第 3 步：预处理扫描 + 确认 ----------
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
                    color: "#555555"
                    elide: Label.ElideMiddle
                    Layout.fillWidth: true
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "返回"
                    onClicked: steps.currentIndex = 0
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
                color: "#fdf3e7"
                border.width: 1
                border.color: "#e0b070"

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
                            color: "#8a5a1a"
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
                color: "#777777"
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
                            color: "#fafafa"
                            border.width: 1
                            border.color: "#d8d8d8"

                            RowLayout {
                                id: rowLayout
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    spacing: 2
                                    RowLayout {
                                        spacing: 8
                                        Label { text: "标题"; font.pixelSize: 12 }
                                        TextField {
                                            text: dialog.scanPlans[index].title
                                            font.pixelSize: 13
                                            Layout.preferredWidth: 240
                                            onTextChanged: dialog.scanPlans[index].title = text
                                        }
                                    }
                                    RowLayout {
                                        spacing: 8
                                        Label { text: "画家"; font.pixelSize: 12 }
                                        TextField {
                                            text: dialog.scanPlans[index].creatorName
                                            placeholderText: "anonymous"
                                            font.pixelSize: 13
                                            Layout.preferredWidth: 160
                                            onTextChanged: dialog.scanPlans[index].creatorName = text
                                        }
                                        Label { text: "日期"; font.pixelSize: 12 }
                                        TextField {
                                            text: dialog.scanPlans[index].creationDate
                                            placeholderText: "YYYY-MM-DD"
                                            font.pixelSize: 13
                                            Layout.preferredWidth: 140
                                            onTextChanged: dialog.scanPlans[index].creationDate = text
                                        }
                                    }
                                    Text {
                                        text: dialog.scanPlans[index].files.length + " 个文件"
                                              + (dialog.scanPlans[index].coverSourcePath
                                                 ? "；封面：" + dialog.scanPlans[index].coverSourcePath
                                                     .split(/[\\/]/).pop() : "；封面默认首张")
                                        color: "#777777"
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

        // ---------- 第 4 步：结果报告 ----------
        ColumnLayout {
            spacing: 12

            Item { Layout.preferredHeight: 30 }

            Text {
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 18
                font.bold: true
                color: lastResult.failed > 0 ? "#c0341d" : "#2d7a2d"
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
                            color: "#fbeae7"
                            Text {
                                anchors.centerIn: parent
                                font.pixelSize: 12
                                color: "#a02a16"
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
