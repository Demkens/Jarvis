// 数据包导入对话框
// 结构：顶栏（通道A 标识数据类型）+ 内容页栈（向导页 / 预处理页 / 结果页）+ 底栏（通道A 导入·关闭）。
// 契约：
//  - 通道A(向导)：类型包 wizard.qml 自绘内容；本对话框提供顶栏与底栏「导入/关闭」。
//    导入 → wizardLoader.item.submit()；失败 → 类型包发 importFailed → 弹出独立警告弹窗。
//    命中库内完全重复 → 类型包发 importBlocked → 弹出重复明细并阻断，不写库、不复位已选文件。
//    成功后类型包自行复位回拖拽区，本对话框不跳结果页、不关闭。
//  - 通道B(预处理)：内容与结果页完全归本对话框，顶/底栏不显示。

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
    standardButtons: Dialog.NoButton   // 关闭/导入由自建底栏控制
    padding: 0      // 弹窗内容区与边框间的内边距

    property string form: ""
    property string channel: "wizard"
    property var scanPlans: []
    property var lastResult: ({})

    // 函数: 顶栏标识——按 form 从已启用类型中取显示名
    function typeDisplayName() {
        var list = typeEngine.enabledTypes
        for (var i = 0; i < list.length; ++i)
            if (list[i].form === dialog.form)
                return list[i].displayName || list[i].form
        return dialog.form
    }

    // 函数: 当前类型是否为整本文本布局（决定预处理页的字段与提交结构）
    // 输入/输出: 无；输出: bool
    function isBookLayout() {
        var list = typeEngine.enabledTypes
        for (var i = 0; i < list.length; ++i) {
            if (list[i].form === dialog.form)
                return list[i].layoutKind === "wholeBookText"
        }
        return false
    }

    // 函数: 预处理计划的一行摘要（图片类型报文件数，文本类型报章节数）
    // 输入: p —— 扫描得到的计划对象；输出: 摘要文本
    function planSummary(p) {
        if (dialog.isBookLayout()) {
            if (p.chapterCount > 0)
                return p.chapterCount + " 章 · " + (p.firstTitle || "") + " → "
                        + (p.lastTitle || "")
            return "章节数导入时统计"
        }
        return (p.files ? p.files.length : 0) + " 个文件"
    }

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

    // 函数: 导入完成, 存储结果（仅通道B 使用）
    //       若结果中存在完全重复项，先弹出重复明细，结果页仍正常展示失败项
    function showResult(result) {
        lastResult = result
        steps.currentIndex = 2
        var duplicates = []
        var list = result.results || []
        for (var i = 0; i < list.length; ++i) {
            if (list[i] && list[i].duplicate)
                duplicates = duplicates.concat(list[i].duplicates || [])
        }
        if (duplicates.length > 0)
            duplicateWarn.show(duplicates)
    }

    // 函数: 取路径中的文件名（含扩展名）
    // 输入: path；输出: 文件名
    function fileName(path) {
        return String(path).split(/[\\/]/).pop()
    }

    // 函数: 字节数 → 可读文本
    // 输入: n —— 字节数；输出: 如 "1.5 MB" 的文本
    function formatBytes(n) {
        if (!n || n <= 0)
            return "0 B"
        var units = ["B", "KB", "MB", "GB"]
        var i = 0
        var v = n
        while (v >= 1024 && i < units.length - 1) {
            v /= 1024
            ++i
        }
        return (i === 0 ? String(v) : v.toFixed(1)) + " " + units[i]
    }

    // 背景显示调整
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // ---------- 顶栏：标识当前导入的数据类型（仅通道A） ----------
        Text {
            visible: dialog.channel === "wizard"
            text: "导入 · " + dialog.typeDisplayName()
            font.pixelSize: 15
            font.bold: true
            Layout.fillWidth: true
        }

        // ---------- 页面切换管理器 ----------
        StackLayout {
            id: steps
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            // ---------- 向导页（通道A：类型包自绘） ----------
            Loader {
                id: wizardLoader
                active: false
                source: dialog.form !== "" ? typeEngine.wizardUrl(dialog.form) : ""
                onLoaded: {
                    item.form = dialog.form
                    // 失败 → 独立警告弹窗；成功由类型包自行复位（不跳结果页）
                    item.importFailed.connect(function (message) { importWarn.show(message) })
                    // 完全重复 → 阻断导入，弹出重复明细并由类型包保留当前选择
                    // 兼容尚未声明该信号的类型包：缺失时退化为普通失败提示
                    if (item.importBlocked !== undefined) {
                        item.importBlocked.connect(function (duplicates) {
                            duplicateWarn.show(duplicates)
                        })
                    }
                }
            }

            // ---------- 预处理扫描 + 确认（通道B） ----------
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
                                var plan = {
                                    title: p.title,
                                    creatorName: p.creatorName,
                                    creationDate: p.creationDate,
                                    coverSourcePath: p.coverSourcePath
                                }
                                // 文本布局以整本 txt 为源，图片布局以文件清单为源
                                if (dialog.isBookLayout())
                                    plan.sourcePath = p.sourcePath
                                else
                                    plan.files = p.files
                                plans.push(plan)
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
                    text: importService.preprocessHint(dialog.form)
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
                                            Label {
                                                text: dialog.isBookLayout() ? "作者" : "画家"
                                                font.pixelSize: 12
                                            }
                                            TextField {
                                                text: dialog.scanPlans[index].creatorName
                                                placeholderText: "佚名"
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
                                            Layout.fillWidth: true
                                            text: dialog.planSummary(dialog.scanPlans[index])
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Label { id: scanRoot; visible: false }
            }

            // ---------- 结果报告（通道B） ----------
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

        // ---------- 底栏：导入 / 关闭（仅通道A） ----------
        RowLayout {
            visible: dialog.channel === "wizard"
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            Button {
                text: "导入"
                highlighted: true
                enabled: wizardLoader.item ? wizardLoader.item.ready : false
                onClicked: if (wizardLoader.item) wizardLoader.item.submit()
            }
            Button {
                text: "关闭"
                onClicked: dialog.close()
            }
        }
    }

    // 导入失败警告（独立弹窗，仅通道A 使用）
    Dialog {
        id: importWarn
        property string message: ""
        function show(text) {
            message = text
            open()
        }
        title: "导入失败"
        modal: true
        standardButtons: Dialog.NoButton
        width: 440
        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: importWarn.message
                color: "#c0341d"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "关闭"
                    highlighted: true
                    onClicked: importWarn.close()
                }
            }
        }
    }

    // 完全重复阻断提示（独立弹窗，通道A/B 共用）
    // 只展示明细，不提供「仍然导入」；关闭后回到导入页，保留已选文件与表单
    Dialog {
        id: duplicateWarn
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        property var duplicates: []
        function show(list) {
            duplicates = list || []
            open()
        }
        title: "检测到完全重复"
        modal: true
        standardButtons: Dialog.NoButton
        width: 560
        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "以下文件与库中已有内容字节级完全相同，已阻断本次导入："
                color: "#c0341d"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(duplicateColumn.implicitHeight, 320)
                clip: true

                Column {
                    id: duplicateColumn
                    width: duplicateWarn.width - 48
                    spacing: 8

                    Repeater {
                        model: duplicateWarn.duplicates
                        delegate: Rectangle {
                            required property var modelData
                            width: duplicateColumn.width
                            height: duplicateItem.implicitHeight + 16
                            radius: 4
                            color: "#f3f5f9"

                            ColumnLayout {
                                id: duplicateItem
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: dialog.fileName(modelData.path) + " · "
                                          + dialog.formatBytes(modelData.size)
                                    color: "#1f2430"
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                }

                                Column {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Repeater {
                                        model: modelData.existing || []
                                        delegate: Text {
                                            required property var modelData
                                            width: parent.width
                                            text: "已存在于：" + (modelData.title || "未命名")
                                                  + " · " + (modelData.relativePath || "")
                                            color: "#5c6472"
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "知道了"
                    highlighted: true
                    onClicked: duplicateWarn.close()
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
