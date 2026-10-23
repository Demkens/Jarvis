// draw 类型包 · 导入向导页（通道A 内容区，开发文档 7.4）
// 职责：为「画作」类型提供单包导入的交互内容，两种状态切换——
//   ① 初始态：全区域虚线拖拽区（拖入或单击选择图片，可多选）；
//   ② 已选态：左右分栏——左侧只读硬性元数据（封面预览 / 预分配 ID / 文件数量 / 大小 / 上传日期），
//              右侧可编辑表单（标题 / 评分 / 创作者 / 创作日期）。
// 边界：本页不碰数据库与实际库文件系统，只产出一份 ImportPlan 映射交给核心执行。
// 与核心（ImportDialog）的契约：
//   输入  form          —— 外层注入的目标类型 form；
//         importService —— 全局上下文属性（导入预览与执行）。
//   输出  ready         —— 底栏「导入」按钮可用性（已选到文件为真）；
//         importFailed  —— 导入失败时发出，外层弹出独立警告弹窗；
//         importBlocked —— 命中库内完全重复时发出，外层弹出重复明细并保留当前选择；
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

    // 已选文件（真实本地绝对路径，顺序即包内序号 001.ext…）
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

    // 函数: 本地路径 → file:/// URL（供 Image.source 使用）
    //       encodeURI 不编码 "/" 与 ":"，恰好保留盘符与路径分隔符；
    //       "%" "#" "?" 先手动转义，避免被 URL 解析吞掉或二次解码
    // 输入: path —— 本地绝对路径；输出: file:/// 开头的 URL
    function localToUrl(path) {
        var p = String(path).replace(/\\/g, "/")
        p = p.replace(/%/g, "%25").replace(/#/g, "%23").replace(/\?/g, "%3F")
        if (p.charAt(0) !== "/")
            p = "/" + p                 // "D:/x.jpg" → "/D:/x.jpg"
        return "file://" + encodeURI(p) // "file://" + "/D:/…" = file:///D:/…
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

    // 函数: 判定路径是否属于受支持的图片格式
    // 输入: path；输出: bool
    function isImageFile(path) {
        var m = String(path).toLowerCase().match(/\.([a-z0-9]+)$/)
        if (!m)
            return false
        return ["jpg", "jpeg", "png", "gif", "bmp", "webp"].indexOf(m[1]) >= 0
    }

    // 函数: URL 列表 → 本地图片路径列表（丢弃空项与非图片，保持原顺序）
    // 输入: urls —— URL 字符串数组；输出: 本地绝对路径数组
    function urlsToLocalPaths(urls) {
        var out = []
        for (var i = 0; i < urls.length; ++i) {
            var p = fileUrlToLocal(urls[i])
            if (p !== "" && isImageFile(p))
                out.push(p)
        }
        return out
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

    // ---- 状态流转 ----

    // 函数: 重新计算只读元数据
    //       文件总字节需读盘，QML 无此能力，交给核心 previewImport
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

    // 函数: 落定已选文件并进入「已选态」（表单字段回到默认值）
    // 输入: paths —— 本地图片绝对路径列表；输出: 无
    function setFiles(paths) {
        var valid = []
        for (var i = 0; i < paths.length; ++i) {
            if (String(paths[i]) !== "")
                valid.push(String(paths[i]))
        }
        if (valid.length === 0)
            return
        files = valid
        titleField.text = baseName(valid[0])    // 默认标题取首图主名
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
            coverSourcePath: ""             // 封面默认取序号第一张
        }
        var result = importService.executePlans(form, [plan])
        var first = (result.results && result.results.length > 0) ? result.results[0] : null
        if (first && first.ok) {
            reset()
        } else if (first && first.duplicate) {
            // 阻断导入，保留当前文件与表单，外层只弹重复明细
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

        // 虚线边框（Rectangle 不支持虚线，故用 Canvas 勾勒）
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

        // 居中提示
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "将文件拖到此处，或单击上传"
                font.pixelSize: 15
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "支持 jpg / jpeg / png / gif / bmp / webp，可多选"
                font.pixelSize: 12
            }
        }

        // 先声明点击层、后声明拖拽层：拖拽层在上负责接收 drop，
        // 点击层在下不受拖拽层影响（DropArea 不消费鼠标事件）
        MouseArea {
            anchors.fill: parent
            onClicked: imagePicker.open()
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

            // 封面预览：首图按比例完整显示（不裁切）
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                radius: 4

                Image {
                    anchors.fill: parent
                    anchors.margins: 8
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    source: page.files.length > 0 ? page.localToUrl(page.files[0]) : ""
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
                        text: page.files.length + " 个"
                        font.pixelSize: 12
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

    // 图片选择（初始态单击拖拽区触发）
    FileDialog {
        id: imagePicker
        title: "选择图片（可多选，按文件名排序）"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["图片 (*.jpg *.jpeg *.png *.gif *.bmp *.webp)"]
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
