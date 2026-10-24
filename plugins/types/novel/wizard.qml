// novel 类型包 · 导入向导页（通道A 内容区，开发文档 7.4）
// 职责：为「小说」类型提供单本书导入的交互内容，两种状态切换——
//   ① 初始态：虚线拖拽区（拖入或单击选择一个 .txt；多选时只取第一个并提示）；
//   ② 已选态：左侧只读分析结果（书名 / 编码 / 识别章数 / 首末章 / 告警），
//              右侧可编辑表单（标题 / 评分 / 作者 / 创作日期）。
// 边界：本页不碰数据库与实际库文件系统；章节切分由核心在 executePlans 内完成。
// 与核心（ImportDialog）的契约：
//   输入  form          —— 外层注入的目标类型 form；
//         importService —— 全局上下文属性（章节分析与执行）。
//   输出  ready         —— 底栏「导入」按钮可用性（已选到一本且有分析结果）；
//         importFailed  —— 导入失败时发出，外层弹出独立警告弹窗；
//         submit()      —— 底栏「导入」点击时由外层调用。
//   文本不走图片查重，故不声明 importBlocked；成功后本页复位回初始态。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import "."

Item {
    id: page

    // 外层注入：本页对应的类型 form
    property string form: ""

    // 已选源文件（整本 txt 的本地绝对路径）
    property string sourcePath: ""

    // 分析结果（由核心 analyzeBook 计算，本页无读盘能力）
    property var analysis: ({})

    // 输出: 底栏「导入」可用性——已选到书且分析成功
    readonly property bool ready: sourcePath !== "" && analysis.ok === true

    // 输出: 导入失败，携带可读原因
    signal importFailed(string message)

    implicitWidth: 760
    implicitHeight: 520

    // 创作日期必填：首次进入预填当天
    Component.onCompleted: if (dateField.text === "") dateField.text = today()

    // ---- 工具函数 ----

    // 函数: file:/// URL → 真实本地路径
    // 输入: url —— FileDialog / DropArea 产出的 URL；输出: 解码后的绝对路径
    function fileUrlToLocal(url) {
        var s = String(url)
        if (s.indexOf("file:///") === 0)
            s = s.substring(8)
        return decodeURIComponent(s)
    }

    // 函数: 取路径中的文件名（含扩展名）
    function fileName(path) {
        return String(path).split(/[\\/]/).pop()
    }

    // 函数: 取主名（去掉最后一个扩展名），用作默认标题
    function baseName(path) {
        return fileName(path).replace(/\.[^.]+$/, "")
    }

    // 函数: 两位补零
    function pad2(n) {
        return n < 10 ? "0" + n : String(n)
    }

    // 函数: 今天的日期（导入日期语义）
    function today() {
        var d = new Date()
        return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate())
    }

    // 函数: 判定是否 txt
    // 输入: path；输出: bool
    function isTxtFile(path) {
        return /\.txt$/i.test(String(path))
    }

    // ---- 状态流转 ----

    // 函数: 落定源文件并进入「已选态」
    // 输入: path —— 整本 txt 的本地绝对路径；输出: 无（分析失败时发 importFailed）
    function setSource(path) {
        var p = String(path)
        if (p === "")
            return
        if (!isTxtFile(p)) {
            page.importFailed("只支持 .txt 文本：" + fileName(p))
            return
        }
        sourcePath = p
        titleField.text = baseName(p)
        creatorField.text = ""
        dateField.text = today()
        scoreSpin.value = 0
        analyze()
        titleField.forceActiveFocus()
    }

    // 函数: 调用核心分析整本书（章数/编码/告警），结果写入 analysis
    // 输入/输出: 无
    function analyze() {
        if (sourcePath === "") {
            analysis = ({})
            return
        }
        analysis = importService.analyzeBook(form, sourcePath)
        if (!analysis.ok)
            page.importFailed(analysis.message || "无法分析该书")
    }

    // 函数: 复位回「初始态」
    // 输入/输出: 无
    function reset() {
        sourcePath = ""
        analysis = ({})
        titleField.text = ""
        creatorField.text = ""
        dateField.text = today()
        scoreSpin.value = 0
    }

    // 函数: 提交单本导入（底栏「导入」触发）
    // 输入/输出: 无；成功→reset() 回初始态，失败→发 importFailed(message)
    function submit() {
        if (!ready)
            return
        if (dateField.text.trim() === "") {
            page.importFailed("创作日期为必填项，请选择日期")
            return
        }
        var plan = {
            title: titleField.text.trim(),
            creatorName: creatorField.text.trim(),
            creationDate: dateField.text.trim(),
            score: scoreSpin.value,
            sourcePath: sourcePath,
            coverSourcePath: ""   // 纯文本包无封面，核心留空封面目录
        }
        var result = importService.executePlans(form, [plan])
        var first = (result.results && result.results.length > 0) ? result.results[0] : null
        if (first && first.ok)
            reset()
        else
            page.importFailed(first && first.message ? first.message
                                                    : (result.message || "导入失败"))
    }

    // ================= 视图 =================

    // ---------- 状态①：拖拽区 ----------
    Item {
        id: dropState
        anchors.fill: parent
        visible: page.sourcePath === ""

        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            radius: 8
            color: dropArea.containsDrag ? "#e8f0ff" : "#fafbfd"
            border.width: 2
            border.color: dropArea.containsDrag ? "#3d6ecd" : "#c9d2e0"

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "把整本小说 .txt 拖到这里"
                    font.pixelSize: 16
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "或点击下方按钮选择文件（一次一本）"
                    font.pixelSize: 12
                    color: "#7a8394"
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: "选择 txt 文件…"
                    onClicked: bookPicker.open()
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 560
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "导入后按类型声明的章节规则切分为章文件；识别不到章节则整本单章导入，内容不丢字。"
                    font.pixelSize: 11
                    color: "#9aa3b2"
                }
            }

            DropArea {
                id: dropArea
                anchors.fill: parent
                onDropped: function (drop) {
                    if (drop.hasUrls && drop.urls.length > 0)
                        page.setSource(page.fileUrlToLocal(drop.urls[0]))
                }
            }
        }
    }

    // ---------- 状态②：左侧只读分析 + 右侧表单 ----------
    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 16
        visible: page.sourcePath !== ""

        // 左：只读分析结果
        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: metaColumn.implicitHeight + 24
                radius: 6
                color: "#f5f7fb"

                ColumnLayout {
                    id: metaColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: page.fileName(page.sourcePath)
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideMiddle
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 4

                        Label { text: "编码"; font.pixelSize: 12 }
                        Label {
                            text: page.analysis.encoding || "—"
                            font.pixelSize: 12
                        }
                        Label { text: "识别章数"; font.pixelSize: 12 }
                        Label {
                            text: page.analysis.chapterCount !== undefined
                                  ? page.analysis.chapterCount + " 章" : "—"
                            font.pixelSize: 12
                        }
                        Label { text: "章节规则"; font.pixelSize: 12 }
                        Label {
                            text: page.analysis.ruleId ? page.analysis.ruleId : "整本单章"
                            font.pixelSize: 12
                        }
                        Label { text: "首章"; font.pixelSize: 12 }
                        Label {
                            Layout.fillWidth: true
                            text: page.analysis.firstTitle || "—"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Label { text: "末章"; font.pixelSize: 12 }
                        Label {
                            Layout.fillWidth: true
                            text: page.analysis.lastTitle || "—"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // 分析告警（编码回退、开篇保留、未识别章节等）
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: page.analysis.warnings || []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "⚠ " + modelData
                            font.pixelSize: 11
                            color: "#b8823a"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Button {
                text: "换一本书"
                onClicked: page.reset()
            }
        }

        // 右：可编辑表单
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            RowLayout {
                spacing: 8
                Label { text: "标题"; font.pixelSize: 12 }
                TextField {
                    id: titleField
                    Layout.fillWidth: true
                    font.pixelSize: 13
                }
            }
            RowLayout {
                spacing: 8
                Label { text: "作者"; font.pixelSize: 12 }
                TextField {
                    id: creatorField
                    Layout.fillWidth: true
                    placeholderText: "留空则为「佚名」"
                    font.pixelSize: 13
                }
            }
            RowLayout {
                spacing: 8
                Label { text: "创作日期"; font.pixelSize: 12 }
                TextField {
                    id: dateField
                    Layout.preferredWidth: 150
                    placeholderText: "YYYY-MM-DD"
                    font.pixelSize: 13
                    readOnly: true
                    MouseArea {
                        anchors.fill: parent
                        onClicked: datePicker.openAt(dateField.text)
                    }
                }
                Button {
                    text: "选日期"
                    onClicked: datePicker.openAt(dateField.text)
                }
            }
            RowLayout {
                spacing: 8
                Label { text: "评分"; font.pixelSize: 12 }
                SpinBox {
                    id: scoreSpin
                    from: 0
                    to: 100
                    editable: true
                }
                Label {
                    text: "（0 = 未评分）"
                    font.pixelSize: 11
                    color: "#9aa3b2"
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // ---------- 文件选择 ----------
    FileDialog {
        id: bookPicker
        title: "选择小说文本"
        nameFilters: ["文本文件 (*.txt)"]
        onAccepted: page.setSource(page.fileUrlToLocal(selectedFile))
    }

    DatePicker {
        id: datePicker
        onDateSelected: function (value) { dateField.text = value }
    }
}
