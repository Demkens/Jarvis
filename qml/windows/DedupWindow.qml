// 图片查重窗口（M9）：
// 职责：
//  - 顶部只保留「查重范围 / 查重程度 / 附件文件夹」三项设置；
//  - 扫描/补指纹为异步任务，窗口在任务期间置为 ApplicationModal，结束或取消后解除；
//  - 两阶段进度（预处理 → 疑似文件处理），可随时终止；
//  - 结果只列被判为重复/近似的文件，带「库内/外部」标识、预览与证据（aHash/dHash 距离）；
//  - 库内结果可跳详情；外部文件按路径预览/用系统程序打开；
//  - 关闭窗口即调用 dedupService.clearSession() 释放外部文件指纹内存会话。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: win

    visible: false
    title: "图片查重"
    width: 1080
    height: 740
    minimumWidth: 900
    minimumHeight: 620

    // 任务期间应用级模态（扫描结束解除）
    modality: win.job !== "" ? Qt.ApplicationModal : Qt.NonModal

    // 当前任务："" / "scan" / "backfill"
    property string job: ""
    // 用户在本轮任务中点了关闭：收到结束信号后再真正关闭
    property bool pendingClose: false
    // 任务状态
    property string phase: ""
    property int progressDone: 0
    property int progressTotal: 0
    // 设置
    property bool allTypes: true
    property var selectedForms: []
    // 查重程度以滑块为唯一数据源，避免双向绑定形成循环
    readonly property int thresholdValue: Math.round(thresholdSlider.value)
    property string externalFolder: ""
    property bool recursive: true
    // 结果与指纹状态
    property var results: []
    property var summary: ({})
    property var fingerprint: ({})
    property int selectedIndex: -1
    property string notice: ""
    // 任务中途切换了库：本轮结果不再可信，收到结束信号后丢弃
    property bool discardResult: false

    // 信号: 请求打开库内数据包详情
    signal detailRequested(int fileId)

    // 函数: 外部入口——显示并激活窗口，同时刷新指纹覆盖情况
    // 输入/输出: 无
    function openWindow() {
        if (!visible)
            win.show()
        win.raise()
        win.requestActivate()
        win.refreshFingerprint()
    }

    // 函数: 组装传给 DedupService 的范围参数
    // 输入: 无（读窗口设置）；输出: 服务层约定的 query 对象
    function buildQuery() {
        return {
            allTypes: win.allTypes,
            forms: win.selectedForms,
            threshold: win.thresholdValue,
            externalFolder: win.externalFolder,
            recursive: win.recursive
        }
    }

    // 函数: 查询指纹覆盖情况（缺内容/缺感知/未建行的包）
    // 输入/输出: 无
    function refreshFingerprint() {
        if (libraryService.currentName === "") {
            win.fingerprint = ({})
            return
        }
        win.fingerprint = dedupService.fingerprintStatus(win.buildQuery())
    }

    // 函数: 范围按钮显示文案
    // 输入/输出: 无；输出: 如「全部类型」「已选 2 类」「未选择类型」
    function scopeText() {
        if (win.allTypes)
            return "全部类型"
        return win.selectedForms.length === 0 ? "未选择类型" : "已选 " + win.selectedForms.length + " 类"
    }

    // 函数: 指纹状态文案
    // 输入/输出: 无
    function fingerprintText() {
        if (!win.fingerprint || win.fingerprint.ok === false)
            return "指纹状态未知"
        var missing = win.fingerprint.missingAny || 0
        var packages = win.fingerprint.packagesWithoutRows || 0
        if (missing === 0 && packages === 0)
            return "指纹齐全"
        var text = "缺指纹文件 " + missing + " 个"
        if (packages > 0)
            text += " · 未建指纹行的旧包 " + packages + " 个"
        return text
    }

    // 函数: 当前设置下缺指纹实际影响的文件数
    //       精确模式只看 content_hash；近似模式额外看 pHash
    // 输入/输出: 无；输出: 影响条数
    function missingImpact() {
        if (!win.fingerprint || win.fingerprint.ok === false)
            return 0
        var packages = win.fingerprint.packagesWithoutRows || 0
        if (win.thresholdValue > 0)
            return (win.fingerprint.missingAny || 0) + packages
        return (win.fingerprint.missingContent || 0) + packages
    }

    // 函数: 当前选中的结果行
    // 输入/输出: 无；输出: 结果对象或 null
    function selectedRow() {
        if (win.selectedIndex < 0 || win.selectedIndex >= win.results.length)
            return null
        return win.results[win.selectedIndex]
    }

    // 函数: 距离文案
    // 输入: row —— 结果行；输出: 展示用距离描述
    function distanceText(row) {
        if (!row)
            return ""
        if (row.representative)
            return "代表项"
        if (row.exact)
            return "完全重复"
        if (row.distance >= 0)
            return "pHash " + row.distance
        return "—"
    }

    // 函数: 证据文案（aHash/dHash 仅作展示，不参与判定）
    // 输入: row —— 结果行；输出: 证据描述
    function evidenceText(row) {
        if (!row || row.representative)
            return ""
        var parts = []
        if (row.aDistance >= 0)
            parts.push("aHash " + row.aDistance)
        if (row.dDistance >= 0)
            parts.push("dHash " + row.dDistance)
        return parts.join(" · ")
    }

    // 函数: 字节数 → 可读文本
    // 输入: n —— 字节数；输出: 如 "1.5 MB"
    function formatBytes(n) {
        if (!n || n <= 0)
            return "0 B"
        var units = ["B", "KB", "MB", "GB"]
        var index = 0
        var value = n
        while (value >= 1024 && index < units.length - 1) {
            value /= 1024
            ++index
        }
        return (index === 0 ? String(value) : value.toFixed(1)) + " " + units[index]
    }

    // 函数: 清空结果，避免切换库后仍展示旧库数据
    // 输入/输出: 无
    function resetResults() {
        win.results = []
        win.summary = ({})
        win.selectedIndex = -1
        win.phase = ""
        win.progressDone = 0
        win.progressTotal = 0
    }

    // 函数: 开始查重（真正下发任务；缺指纹提示由调用方处理）
    // 输入/输出: 无
    function startScan() {
        var result = dedupService.startScan(win.buildQuery())
        if (!result.ok) {
            win.notice = result.message
            return
        }
        win.resetResults()
        win.job = "scan"
        win.pendingClose = false
        win.discardResult = false
        win.phase = "prepare"
        win.progressDone = 0
        win.progressTotal = 0
        win.notice = ""
    }

    // 函数: 开始补指纹
    // 输入/输出: 无
    function startBackfill() {
        var result = dedupService.startBackfill(win.buildQuery())
        if (!result.ok) {
            win.notice = result.message
            return
        }
        if (result.nothing) {
            win.notice = result.message || "没有需要补指纹的文件"
            win.refreshFingerprint()
            return
        }
        win.job = "backfill"
        win.pendingClose = false
        win.discardResult = false
        win.phase = "backfill"
        win.progressDone = 0
        win.progressTotal = result.total || 0
        win.notice = ""
    }

    // 函数: 设置单个类型的选中状态（全部类型与多选互斥）
    // 输入: form 类型标识、checked 是否选中
    function setFormSelected(form, checked) {
        // 「全部类型」切换时会把各类型复选框程序化复位，这类取消不改变模式
        if (win.allTypes && !checked)
            return
        win.allTypes = false
        var list = win.selectedForms.slice()
        var index = list.indexOf(form)
        if (checked && index < 0)
            list.push(form)
        else if (!checked && index >= 0)
            list.splice(index, 1)
        win.selectedForms = list
        win.refreshFingerprint()
    }

    // 函数: 切换「全部类型」
    // 输入: checked 是否选中全部类型
    function setAllTypes(checked) {
        win.allTypes = checked
        if (checked) {
            win.selectedForms = []
            for (var i = 0; i < typeRepeater.count; ++i)
                typeRepeater.itemAt(i).checked = false
        }
        win.refreshFingerprint()
    }

    // 关闭：任务中先请求取消，等结束信号后再关闭
    onClosing: function (close) {
        if (win.job !== "") {
            close.accepted = false
            win.pendingClose = true
            dedupService.cancel()
            win.notice = "正在停止…"
            return
        }
        dedupService.clearSession()
        win.resetResults()
    }

    // 服务层信号：进度与结果
    Connections {
        target: dedupService

        function onScanProgress(phase, done, total) {
            win.phase = phase
            win.progressDone = done
            win.progressTotal = total
        }

        function onScanFinished(resultSummary, rows) {
            win.job = ""
            if (win.discardResult) {
                win.discardResult = false
                win.notice = "库已切换，本轮查重结果已丢弃"
                if (win.pendingClose) {
                    win.pendingClose = false
                    win.close()
                }
                return
            }
            win.results = rows
            win.summary = resultSummary
            win.selectedIndex = rows.length > 0 ? 0 : -1
            win.phase = ""
            if (resultSummary.canceled)
                win.notice = "已取消查重"
            else
                win.notice = "查重完成：" + (resultSummary.groups || 0) + " 组 · "
                    + (resultSummary.files || 0) + " 个文件"
                    + ((resultSummary.skippedFiles || 0) > 0
                       ? " · 跳过非图片 " + resultSummary.skippedFiles + " 个" : "")
            win.refreshFingerprint()
            if (win.pendingClose) {
                win.pendingClose = false
                win.close()
            }
        }

        function onBackfillProgress(done, total) {
            win.progressDone = done
            win.progressTotal = total
        }

        function onBackfillFinished(resultSummary) {
            win.job = ""
            win.phase = ""
            if (win.discardResult) {
                win.discardResult = false
                win.notice = "库已切换，本轮补指纹结果未写入"
                if (win.pendingClose) {
                    win.pendingClose = false
                    win.close()
                }
                return
            }
            win.notice = resultSummary.ok
                ? ("补指纹完成：写入 " + (resultSummary.written || 0) + " 行，失败 "
                   + (resultSummary.failed || 0) + " 个")
                : (resultSummary.message || "补指纹失败")
            win.refreshFingerprint()
            if (win.pendingClose) {
                win.pendingClose = false
                win.close()
            }
        }
    }

    // 切库后旧结果失效
    Connections {
        target: libraryService
        function onCurrentChanged() {
            if (win.job !== "") {
                win.discardResult = true
                dedupService.cancel()
            }
            win.resetResults()
            win.refreshFingerprint()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // ---------- 查重范围 ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "查重范围"
                font.pixelSize: 13
                Layout.preferredWidth: 72
            }

            Button {
                id: scopeButton
                text: win.scopeText()
                enabled: win.job === ""
                onClicked: typePopup.open()
            }

            Text {
                Layout.fillWidth: true
                text: win.allTypes ? "当前库全部类型"
                                  : (win.selectedForms.length === 0
                                     ? "请至少选择一个类型" : "仅比较所选类型的数据包")
                color: "#6b7280"
                font.pixelSize: 12
            }
        }

        // ---------- 查重程度 ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "查重程度"
                font.pixelSize: 13
                Layout.preferredWidth: 72
            }

            Slider {
                id: thresholdSlider
                Layout.fillWidth: true
                from: 0
                to: 16
                stepSize: 1
                snapMode: Slider.SnapAlways
                enabled: win.job === ""
                // 默认 8：约 12% 像素差异以内视为近似
                value: 8
            }

            Label {
                Layout.preferredWidth: 190
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 12
                color: "#374151"
                text: win.thresholdValue === 0
                      ? "精确匹配（字节级一致）"
                      : "近似匹配 · pHash ≤ " + win.thresholdValue
            }
        }

        // ---------- 附件文件夹 ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "附件文件夹"
                font.pixelSize: 13
                Layout.preferredWidth: 72
            }

            TextField {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                font.pixelSize: 12
                text: win.externalFolder
                placeholderText: "（不扫描外部文件）"
            }

            Button {
                text: "浏览…"
                enabled: win.job === ""
                onClicked: externalPicker.open()
            }

            Button {
                text: "清除"
                enabled: win.job === "" && win.externalFolder !== ""
                onClicked: win.externalFolder = ""
            }

            CheckBox {
                text: "包含子文件夹"
                checked: win.recursive
                enabled: win.job === ""
                onToggled: win.recursive = checked
            }
        }

        Text {
            Layout.fillWidth: true
            font.pixelSize: 11
            color: "#9ca3af"
            text: "本期仅比较图片文件（jpg/png/gif/bmp/webp）；视频查重暂未启用。"
        }

        // ---------- 指纹状态与动作 ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                Layout.fillWidth: true
                font.pixelSize: 12
                color: (win.fingerprint.missingAny || 0) > 0 ? "#b45309" : "#6b7280"
                text: win.fingerprintText()
            }

            Button {
                text: "补指纹"
                enabled: win.job === "" && libraryService.currentName !== ""
                         && ((win.fingerprint.missingAny || 0) > 0
                             || (win.fingerprint.packagesWithoutRows || 0) > 0)
                onClicked: win.startBackfill()
            }

            Button {
                text: "开始查重"
                highlighted: true
                enabled: win.job === "" && libraryService.currentName !== ""
                         && (win.allTypes || win.selectedForms.length > 0)
                onClicked: {
                    if (win.missingImpact() > 0)
                        missingPrompt.open()
                    else
                        win.startScan()
                }
            }

            Button {
                text: "停止"
                visible: win.job !== ""
                enabled: win.job !== "" && win.notice !== "正在停止…"
                onClicked: {
                    dedupService.cancel()
                    win.notice = "正在停止…"
                }
            }
        }

        // ---------- 进度 ----------
        RowLayout {
            Layout.fillWidth: true
            visible: win.job !== ""
            spacing: 10

            Label {
                Layout.preferredWidth: 210
                font.pixelSize: 12
                text: win.phase === "prepare" ? "预处理：计算外部文件指纹"
                      : win.phase === "backfill" ? "补指纹：计算中"
                      : "疑似文件处理：比较指纹"
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: win.progressTotal > 0 ? win.progressTotal : 1
                value: win.progressDone
                indeterminate: win.progressTotal <= 0 && win.job !== ""
            }

            Label {
                Layout.preferredWidth: 110
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 12
                text: win.progressTotal > 0
                      ? win.progressDone + " / " + win.progressTotal : ""
            }
        }

        // ---------- 结果区 ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // 左：结果列表
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#ffffff"
                border.color: "#e5e7eb"
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        font.pixelSize: 12
                        color: "#6b7280"
                        text: win.results.length === 0
                              ? "暂无重复结果"
                              : "共 " + (win.summary.groups || 0) + " 组 · "
                                + win.results.length + " 个文件（按组排列）"
                    }

                    ListView {
                        id: resultList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.results
                        spacing: 0
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property int index
                            required property var modelData

                            width: resultList.width
                            height: 76
                            color: index === win.selectedIndex ? "#e8efff" : "transparent"

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: "#f1f2f4"
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 10

                                Image {
                                    Layout.preferredWidth: 56
                                    Layout.preferredHeight: 56
                                    source: modelData.previewUrl
                                    sourceSize: Qt.size(56, 56)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.title || "（无标题）"
                                            elide: Text.ElideRight
                                            font.pixelSize: 13
                                            font.bold: true
                                        }

                                        Rectangle {
                                            implicitWidth: originLabel.implicitWidth + 10
                                            implicitHeight: 18
                                            radius: 3
                                            color: modelData.origin === "external"
                                                   ? "#fff4e5" : "#e8f3ff"
                                            Text {
                                                id: originLabel
                                                anchors.centerIn: parent
                                                font.pixelSize: 11
                                                color: modelData.origin === "external"
                                                       ? "#b45309" : "#1d4ed8"
                                                text: modelData.origin === "external"
                                                      ? "外部" : "库内"
                                            }
                                        }

                                        Text {
                                            font.pixelSize: 11
                                            color: "#6b7280"
                                            text: modelData.typeName || ""
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        font.pixelSize: 11
                                        color: "#6b7280"
                                        elide: Text.ElideMiddle
                                        text: modelData.origin === "library"
                                              ? ("包 #" + modelData.fileId + " · "
                                                 + (modelData.relativePath || ""))
                                              : (modelData.path || "")
                                    }

                                    Text {
                                        font.pixelSize: 11
                                        color: "#374151"
                                        text: win.distanceText(modelData)
                                            + (win.evidenceText(modelData) !== ""
                                               ? " · " + win.evidenceText(modelData) : "")
                                    }
                                }

                                Text {
                                    font.pixelSize: 11
                                    color: "#6b7280"
                                    text: win.formatBytes(modelData.size)
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: win.selectedIndex = index
                            }
                        }
                    }
                }
            }

            // 右：选中项预览
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: "#ffffff"
                border.color: "#e5e7eb"
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Image {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 300
                        fillMode: Image.PreserveAspectFit
                        source: win.selectedRow() ? win.selectedRow().previewUrl : ""
                        asynchronous: true
                        cache: false
                    }

                    Text {
                        Layout.fillWidth: true
                        text: win.selectedRow() ? (win.selectedRow().title || "") : "选择左侧结果查看预览"
                        font.pixelSize: 14
                        font.bold: true
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                    }

                    Text {
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#6b7280"
                        wrapMode: Text.WordWrap
                        text: {
                            var row = win.selectedRow()
                            if (!row)
                                return ""
                            var lines = []
                            lines.push(row.origin === "external" ? "来源：外部文件" : "来源：库内数据包")
                            if (row.origin === "library") {
                                lines.push("包 #" + row.fileId + " · " + (row.typeName || ""))
                                if (row.creatorName)
                                    lines.push("创作者：" + row.creatorName)
                                lines.push("包内路径：" + (row.relativePath || ""))
                            }
                            lines.push("大小：" + win.formatBytes(row.size))
                            lines.push(win.distanceText(row)
                                       + (win.evidenceText(row) !== ""
                                          ? " · " + win.evidenceText(row) : ""))
                            return lines.join("\n")
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Button {
                        Layout.fillWidth: true
                        text: "打开详情"
                        visible: win.selectedRow() !== null
                                 && win.selectedRow().origin === "library"
                        onClicked: win.detailRequested(win.selectedRow().fileId)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "用系统程序打开"
                        visible: win.selectedRow() !== null
                                 && win.selectedRow().origin === "external"
                        onClicked: Qt.openUrlExternally(win.selectedRow().previewUrl)
                    }
                }
            }
        }

        // ---------- 底栏 ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                Layout.fillWidth: true
                font.pixelSize: 12
                color: "#6b7280"
                elide: Text.ElideRight
                text: win.notice
            }

            Button {
                text: "关闭"
                onClicked: {
                    if (win.job !== "") {
                        win.pendingClose = true
                        dedupService.cancel()
                        win.notice = "正在停止…"
                    } else {
                        win.close()
                    }
                }
            }
        }
    }

    // ---------- 类型多选弹层 ----------
    Popup {
        id: typePopup
        width: 260
        x: Math.round((win.width - width) / 2)
        y: 110
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 10

        contentItem: ColumnLayout {
            spacing: 2

            Label {
                text: "查重范围"
                font.pixelSize: 13
                font.bold: true
            }

            CheckBox {
                id: allTypesCheck
                text: "全部类型"
                checked: win.allTypes
                onToggled: win.setAllTypes(checked)
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: "#e5e7eb"
            }

            Repeater {
                id: typeRepeater
                model: typeEngine.enabledTypes

                delegate: CheckBox {
                    required property var modelData
                    text: modelData.displayName || modelData.form
                    checked: win.selectedForms.indexOf(modelData.form) >= 0
                    onToggled: win.setFormSelected(modelData.form, checked)
                }
            }

            Text {
                Layout.fillWidth: true
                visible: typeRepeater.count === 0
                font.pixelSize: 12
                color: "#6b7280"
                text: "当前库没有可用类型"
            }
        }
    }

    // ---------- 缺指纹提示 ----------
    Dialog {
        id: missingPrompt
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 420
        modal: true
        title: "缺少指纹"
        standardButtons: Dialog.NoButton
        padding: 16

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "有 " + win.missingImpact()
                      + " 项缺少指纹，这些文件将无法参与查重。\n"
                      + "建议先补指纹（需要读取并解码图片），或仍按现有指纹查重。"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: "先补指纹"
                    onClicked: {
                        missingPrompt.close()
                        win.startBackfill()
                    }
                }

                Button {
                    text: "仍要查重"
                    onClicked: {
                        missingPrompt.close()
                        win.startScan()
                    }
                }

                Button {
                    text: "取消"
                    onClicked: missingPrompt.close()
                }
            }
        }
    }

    // ---------- 附件文件夹选择 ----------
    FolderDialog {
        id: externalPicker
        title: "选择附件文件夹"
        onAccepted: {
            var local = String(selectedFolder)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            win.externalFolder = decodeURIComponent(local) // %20 等还原为真实路径
        }
    }
}
