// 视频类类型包 · 导入向导页（film / modelfilm / episode 共用，通道A 内容区，开发文档 7.4）
// 职责：视频媒体包的导入交互，两种状态切换——
//   ① 初始态：全区域虚线拖拽区（拖入或单击选择文件）；
//   ② 已选态：左右分栏——左侧只读硬性元数据（封面占位 / 预分配 ID / 文件数量 / 大小 / 上传日期），
//              右侧可编辑表单（标题 / 评分 / 创作者 / 创作日期）。
// 单包文件数由 maxFiles 控制：1 = 单视频包（film/modelfilm，多选即拒绝）；
//                              0 = 不限（episode 剧集，多选的文件按文件名自然序编号为 001、002…）。
// 边界：不碰数据库与实际库文件系统，只产出一份 ImportPlan 映射交给核心执行；
//       封面由核心按"无图片视频包"生成占位封面（本页不指定封面来源）。
// 与核心（ImportDialog）的契约：
//   输入  form          —— 外层注入的目标类型 form；
//         maxFiles      —— 单包允许的最大文件数（0 = 不限，缺省 0）；
//         importService —— 全局上下文属性（导入预览与执行）；
//         typeEngine    —— 全局上下文属性（按类型声明的媒体后缀过滤文件）。
//   输出  ready         —— 底栏「导入」按钮可用性（已选到文件为真）；
//         importFailed  —— 导入失败/超量被拒时发出，外层弹出警告弹窗；
//         importBlocked —— 命中库内完全重复时发出（视频不参与图片查重，基本不会触发）；
//         submit()      —— 底栏「导入」点击时由外层调用。
//   成功后本页自行复位回初始态：不关闭弹窗、不跳结果页。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import "."

Item {
    id: page

    // 外层注入：本页对应的类型 form
    property string form: ""

    // 单包文件数上限：1 = 单媒体包；0 = 不限（多文件按文件名自然序编号入包）
    property int maxFiles: 0

    // 已选文件（真实本地绝对路径；顺序即包内序号，已按文件名自然序排列）
    property var files: []

    // 只读元数据（由核心 previewImport 计算，本页无文件读盘能力）
    property int previewId: 0
    property real previewSize: 0
    property string previewDate: ""

    // 输出: 底栏「导入」可用性——已选到文件即可导入
    readonly property bool ready: files.length > 0

    // 输出: 导入失败，携带可读原因
    signal importFailed(string message)

    // 输出: 命中库内完全重复，携带重复明细；外层仅弹提示，不复位本页已选文件
    signal importBlocked(var duplicates)

    // 无布局管理器托底时的隐式尺寸（StackLayout 会覆写为填充尺寸）
    implicitWidth: 760
    implicitHeight: 520

    // 创作日期为必填项：首次进入即预填当天，避免用户未选日期就提交而报错
    Component.onCompleted: if (dateField.text === "") dateField.text = today()

    // ---- 路径与格式化工具函数 ----

    // 函数: file:/// URL → 真实本地路径（FileDialog / DropArea 产出皆为 URL）
    // 输入: url —— file:/// 开头的 URL 字符串；输出: 解码后的本地绝对路径
    function fileUrlToLocal(url) {
        var s = String(url)
        if (s.indexOf("file:///") === 0)
            s = s.substring(8)          // "file:///" 长度 8
        return decodeURIComponent(s)
    }

    // 函数: 取路径中的文件名（含扩展名）
    // 输入: path；输出: 文件名
    function fileName(path) {
        return String(path).split(/[\\/]/).pop()
    }

    // 函数: 取主名（去掉最后一个扩展名），用作默认标题
    // 输入: path；输出: 主名
    function baseName(path) {
        return fileName(path).replace(/\.[^.]+$/, "")
    }

    // 函数: 按文件名的"自然序"比较（数字段按数值大小，其余按字符）
    // 输入: a, b —— 本地路径；输出: 负数/0/正数（供 Array.sort 使用）
    // 用途：多集导入时 "第10集" 应排在 "第2集" 之后，避免字典序把 10 排到 2 前面
    function naturalCompare(a, b) {
        var sa = fileName(a).toLowerCase()
        var sb = fileName(b).toLowerCase()
        var partsA = sa.match(/(\d+)|(\D+)/g) || []
        var partsB = sb.match(/(\d+)|(\D+)/g) || []
        for (var i = 0; i < Math.min(partsA.length, partsB.length); ++i) {
            var na = parseInt(partsA[i], 10)
            var nb = parseInt(partsB[i], 10)
            if (!isNaN(na) && !isNaN(nb)) {
                if (na !== nb)
                    return na - nb
            } else if (partsA[i] !== partsB[i]) {
                return partsA[i] < partsB[i] ? -1 : 1
            }
        }
        return partsA.length - partsB.length
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

    // 函数: 两位补零
    // 输入: n —— 整数；输出: 长度 2 的字符串
    function pad2(n) {
        return n < 10 ? "0" + n : String(n)
    }

    // 函数: 今天的日期（导入日期语义）
    // 输入: 无；输出: "YYYY-MM-DD"
    function today() {
        var d = new Date()
        return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate())
    }

    // 函数: 文件对话框过滤器（后缀来自类型包 media 声明，避免 QML 硬编码格式清单）
    // 输入/输出: 无；输出: nameFilters 数组
    function nameFilters() {
        var suffixes = typeEngine.mediaSuffixes(form)
        if (!suffixes || suffixes.length === 0)
            return ["所有文件 (*)"]
        var patterns = []
        for (var i = 0; i < suffixes.length; ++i)
            patterns.push("*." + suffixes[i])
        return ["媒体 (" + patterns.join(" ") + ")", "所有文件 (*)"]
    }

    // 函数: URL 列表 → 本地媒体路径列表（丢弃空项与不受该类型支持的文件，保持原顺序）
    // 输入: urls —— URL 字符串数组；输出: 本地绝对路径数组
    function urlsToLocalPaths(urls) {
        var out = []
        for (var i = 0; i < urls.length; ++i) {
            var p = fileUrlToLocal(urls[i])
            if (p !== "" && typeEngine.acceptsFile(form, p))
                out.push(p)
        }
        return out
    }

    // ---- 状态流转 ----

    // 函数: 重新计算只读元数据（文件总字节需读盘，交给核心 previewImport）
    // 输入: 无（读 page.files）；输出: 无（写 previewId / previewSize / previewDate）
    function refreshPreview() {
        if (files.length === 0) {
            previewId = 0
            previewSize = 0
            previewDate = ""
            return
        }
        previewDate = today()
        var r = importService.previewImport(files)
        if (r.ok) {
            previewId = r.id
            previewSize = r.size
        } else {
            previewId = 0
            previewSize = 0
            page.importFailed(r.message || "无法读取预览信息")
        }
    }

    // 函数: 落定已选文件并进入「已选态」；超过 maxFiles（maxFiles > 0 时）即拒绝
    // 输入: paths —— 本地媒体绝对路径列表；输出: 无（被拒时保持原状态并发 importFailed）
    function setFiles(paths) {
        var valid = []
        for (var i = 0; i < paths.length; ++i) {
            if (String(paths[i]) !== "")
                valid.push(String(paths[i]))
        }
        if (valid.length === 0)
            return
        if (page.maxFiles > 0 && valid.length > page.maxFiles) {
            page.importFailed("该类型每包仅允许 " + page.maxFiles + " 个文件，请单个导入")
            return
        }
        // 多文件按文件名自然序排列：集数文件名（如 第2集 / 第10集）按数值而非字典序排
        if (valid.length > 1)
            valid.sort(function (a, b) { return page.naturalCompare(a, b) })
        files = valid
        // 单文件包默认用文件名主名；多文件包（剧集）不预设，避免把"第01集"当剧名
        titleField.text = valid.length === 1 ? baseName(valid[0]) : ""
        creatorField.text = ""                  // 留空 → 核心落「佚名」
        dateField.text = today()                // 必填，默认当天（格式固定 YYYY-MM-DD）
        scoreSpin.value = 0                     // 0 = 未评分
        refreshPreview()
        titleField.forceActiveFocus()           // 表单习惯：进入即可直接打字
    }

    // 函数: 复位回「初始态」（导入成功、或点击「返回」时调用）
    // 输入/输出: 无
    function reset() {
        files = []
        previewId = 0
        previewSize = 0
        previewDate = ""
        titleField.text = ""
        creatorField.text = ""
        dateField.text = today()                // 必填项复位为当天，不置空
        scoreSpin.value = 0
    }

    // 函数: 提交单包导入（底栏「导入」触发）
    // 输入/输出: 无；成功→reset() 回初始态，失败→发 importFailed(message)
    function submit() {
        if (!ready)
            return
        // 创作日期必填（默认已填当天，此处防御用户清空后提交）
        if (dateField.text.trim() === "") {
            page.importFailed("创作日期为必填项，请选择日期")
            return
        }
        var plan = {
            title: titleField.text.trim(),
            creatorName: creatorField.text.trim(),
            creationDate: dateField.text.trim(),
            score: scoreSpin.value,
            files: files,
            coverSourcePath: ""             // 视频包无图：由核心生成占位封面
        }
        var result = importService.executePlans(form, [plan])
        var first = (result.results && result.results.length > 0) ? result.results[0] : null
        if (first && first.ok) {
            reset()
        } else if (first && first.duplicate) {
            page.importBlocked(first.duplicates || [])
        } else {
            page.importFailed(first && first.message ? first.message
                                                     : (result.message || "导入失败"))
        }
    }

    // ================= 视图 =================

    // ---------- 状态①：全区域虚线拖拽区 ----------
    Item {
        id: dropState
        anchors.fill: parent
        visible: page.files.length === 0

        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = 2
                ctx.setLineDash([7, 7])
                ctx.strokeRect(1, 1, width - 2, height - 2)
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "将视频文件拖到此处，或单击选择"
                font.pixelSize: 15
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: page.maxFiles === 1
                      ? "本类型每包仅 1 个文件"
                      : "可多选，包内按文件名顺序编号为 001、002…"
                font.pixelSize: 12
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: videoPicker.open()
        }
        DropArea {
            anchors.fill: parent
            onDropped: function (drop) {
                if (drop.hasUrls)
                    page.setFiles(page.urlsToLocalPaths(drop.urls))
            }
        }
    }

    // ---------- 状态②：左右分栏 ----------
    RowLayout {
        id: splitState
        anchors.fill: parent
        visible: page.files.length > 0
        spacing: 14

        // ===== 左：只读硬性元数据 =====
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            spacing: 10

            Button {
                text: "← 返回"
                flat: true
                onClicked: page.reset()
            }

            // 封面占位：视频无法在向导内解码预览，此处明示"核心将生成占位封面"
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                radius: 4
                color: "#3a3f4a"

                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "▶"
                        color: "#f2f4f8"
                        font.pixelSize: 42
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "视频封面占位"
                        color: "#f2f4f8"
                        font.pixelSize: 12
                    }
                }
            }

            // 只读元数据卡片
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: metaGrid.implicitHeight + 20
                radius: 4

                GridLayout {
                    id: metaGrid
                    anchors.fill: parent
                    anchors.margins: 10
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6

                    Label { text: "预分配 ID"; font.pixelSize: 12 }
                    Text {
                        text: page.previewId > 0 ? "#" + page.previewId : "—"
                        font.pixelSize: 12
                    }

                    Label { text: "文件数量"; font.pixelSize: 12 }
                    Text {
                        // 单媒体包报文件名；多文件包报数量（集列表长度）
                        text: page.maxFiles === 1
                              ? (page.files.length > 0 ? page.fileName(page.files[0]) : "—")
                              : page.files.length + " 个"
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: 190
                    }

                    Label { text: "文件大小"; font.pixelSize: 12 }
                    Text {
                        text: page.formatBytes(page.previewSize)
                        font.pixelSize: 12
                    }

                    Label { text: "上传日期"; font.pixelSize: 12 }
                    Text {
                        text: page.previewDate
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ===== 右：可编辑表单 =====
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Label { text: "标题"; font.pixelSize: 12 }
            TextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: "未命名"
                font.pixelSize: 13
            }

            Item { Layout.preferredHeight: 6 }

            Label { text: "评分"; font.pixelSize: 12 }
            SpinBox {
                id: scoreSpin
                from: 0
                to: 100
                value: 0
                editable: true
            }

            Item { Layout.preferredHeight: 6 }

            Label { text: "创作者"; font.pixelSize: 12 }
            TextField {
                id: creatorField
                Layout.fillWidth: true
                placeholderText: "佚名"
                font.pixelSize: 13
            }

            Item { Layout.preferredHeight: 6 }

            Label { text: "创作日期（必填）"; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: dateField
                    Layout.fillWidth: true
                    readOnly: true               // 仅经日历选择，避免非法格式
                    placeholderText: "YYYY-MM-DD"
                    font.pixelSize: 13
                }
                Button {
                    text: "选择日期"
                    onClicked: datePicker.openAt(dateField.text)
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    // ---------- 附属弹层 ----------

    // 视频文件选择（初始态单击拖拽区触发；maxFiles === 1 时单选，否则多选）
    // nameFilters 用绑定形式：form 由外层 Loader.onLoaded 注入，晚于本组件创建，
    // 一次性赋值会在 form 为空时退化为"所有文件"
    FileDialog {
        id: videoPicker
        title: page.maxFiles === 1 ? "选择视频文件" : "选择视频文件（可多选，按文件名排序）"
        fileMode: page.maxFiles === 1 ? FileDialog.OpenFile : FileDialog.OpenFiles
        nameFilters: page.nameFilters()
        onAccepted: page.setFiles(page.urlsToLocalPaths(selectedFiles))
    }

    // 创作日期日历（自建，Qt Quick Controls 无内置 DatePicker）
    DatePicker {
        id: datePicker
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        onDateSelected: function (value) { dateField.text = value }
    }
}
