// draw 类型包 · 导入向导页（开发文档 7.4）
// 被核心导入对话框通过 Loader 嵌入；注入：
//   form          本页对应的类型 form（外层设置）
//   importService / typeEngine 为全局上下文属性（组合根注入）
// 产出：一组 ImportPlan 映射，提交给核心导入执行器；本页不碰数据库与存储落位。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: page

    // 外层（ImportDialog）设置
    property string form: ""

    // 提交完成（无论成败）通知外层切到结果页
    signal plansSubmitted(var result)

    // 分组模型：每次「添加一组图片」生成一个包
    property var groups: []

    function fileUrlToLocal(url) {
        // FileDialog 返回 file:/// URL；统一转本地路径（Win: file:///D:/x → D:/x）
        var s = String(url)
        if (s.indexOf("file:///") === 0)
            return s.substring("file:///".length)
        return s
    }

    function submit() {
        var plans = []
        for (var i = 0; i < groups.length; ++i) {
            var g = groups[i]
            if (g.files.length === 0)
                continue
            plans.push({
                title: g.title,
                creatorName: creatorField.text.trim(),
                creationDate: dateField.text.trim(),
                files: g.files,
                coverSourcePath: ""   // 封面默认取序号第一张
            })
        }
        if (plans.length === 0)
            return
        submitBusy.running = true
        var result = importService.executePlans(form, plans)
        submitBusy.running = false
        page.plansSubmitted(result)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Text {
            text: "向导导入：每次「添加一组图片」即为一个差分图集包；组内图片按文件名排序为 001、002…"
            color: "#666666"
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        // 批量字段（应用到本次全部组）
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "画家（批量）"; font.pixelSize: 13 }
            TextField {
                id: creatorField
                Layout.preferredWidth: 180
                placeholderText: "留空为 anonymous"
                font.pixelSize: 13
            }
            Label { text: "创作日期（批量）"; font.pixelSize: 13 }
            TextField {
                id: dateField
                Layout.preferredWidth: 160
                placeholderText: "YYYY / YYYY-MM / YYYY-MM-DD"
                font.pixelSize: 13
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "＋ 添加一组图片"
                enabled: !submitBusy.running
                onClicked: filePicker.open()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: groups.length > 0 ? "导入 " + groups.length + " 个包" : "导入"
                highlighted: true
                enabled: groups.length > 0 && !submitBusy.running
                onClicked: page.submit()
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Column {
                width: page.width - 28
                spacing: 8

                Repeater {
                    model: page.groups
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        width: parent.width
                        height: 72
                        radius: 4
                        color: "#f7f7f7"
                        border.width: 1
                        border.color: "#d8d8d8"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Text {
                                text: (index + 1) + "."
                                font.bold: true
                                font.pixelSize: 14
                            }
                            ColumnLayout {
                                spacing: 2
                                TextField {
                                    text: page.groups[index].title
                                    font.pixelSize: 13
                                    Layout.preferredWidth: 260
                                    onTextChanged: page.groups[index].title = text
                                }
                                Text {
                                    text: page.groups[index].files.length + " 张图片，封面默认首张："
                                          + decodeURIComponent(
                                              String(page.groups[index].files[0])
                                                  .split("/").pop().split("\\").pop())
                                    color: "#777777"
                                    font.pixelSize: 11
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "移除该组"
                                flat: true
                                enabled: !submitBusy.running
                                onClicked: page.groups.splice(index, 1)
                            }
                        }
                    }
                }

                Text {
                    visible: page.groups.length === 0
                    text: "尚未添加图片。点击上方按钮选择一张或多张图片。"
                    color: "#999999"
                    font.pixelSize: 13
                    topPadding: 24
                }
            }
        }
    }

    FileDialog {
        id: filePicker
        title: "选择图片（可多选，按文件名排序）"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["图片 (*.jpg *.jpeg *.png *.gif *.bmp *.webp)"]
        onAccepted: {
            var files = []
            for (var i = 0; i < selectedFiles.length; ++i)
                files.push(page.fileUrlToLocal(selectedFiles[i]))
            var base = files.length > 0
                ? decodeURIComponent(String(files[0]).split("/").pop().split("\\").pop()
                                     .replace(/\.[^.]+$/, ""))
                : "untitled"
            page.groups.push({title: base, files: files})
        }
    }

    BusyIndicator {
        id: submitBusy
        running: false
        anchors.centerIn: parent
    }
}
