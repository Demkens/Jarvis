import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../dialogs"

// 独立详情窗口：元数据（封面/标题/标签/字段）+ 包内文件结构化预览。
// 数据源：typeEngine.packageDetail(id) 取元数据；createPackageView(id) 取包内文件清单；
//        标签经 typeEngine.fileTags(id) 取 id 列表、按 typeEngine.tagTree 映射为名称。
// 编辑弹窗由本窗口自带实例（见文件末 EditPackageDialog）：Popup 只能渲染在所属窗口的 overlay 内，
// 若复用主窗口中的实例，会因详情页是独立顶层窗口而把弹窗压在下层。
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
    // 文本包的章节行：{kind: "volume"|"chapter", title, number, size}
    property var chapterRows: []
    property bool isTextPackage: false
    property string detailTitle: ""
    // 当前包已绑定标签的显示名（按 tagTree DFS 序映射后顺序与 fileTags 一致）
    property var tagNames: []

    // 封面显示尺寸：按封面长宽比(宽/高，存于 file.aspect_ratio)推算，宽度以面板内容宽度为上限、
    // 高度以 coverMaxHeight 为上限——箱体比例恒等于图片比例，配合 PreserveAspectFit 可完整显示且无裁切。
    // coverMaxHeight 随面板可用高度自适应（预留约 300px 给标题/标签/字段/间距），
    // 避免窗口缩到最小高度时封面把下方元数据挤出可视区
    readonly property real coverMaxHeight: Math.min(260, Math.max(140, metaColumn.height - 300))
    readonly property real coverAspect: window.detail.aspect_ratio > 0 ? window.detail.aspect_ratio : 1.5
    readonly property real coverWidth: Math.min(metaColumn.width, window.coverMaxHeight * window.coverAspect)
    readonly property real coverHeight: window.coverWidth / window.coverAspect

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

    // 函数: 读取当前包已绑定标签的显示名列表
    // 输入: 无（读 window.packageId / typeEngine.fileTags）；输出: 名称数组（id 无对应标签时以 "#id" 兜底）
    function loadTagNames() {
        var ids = typeEngine.fileTags(window.packageId) || []
        var tree = typeEngine.tagTree || []
        var byId = {}
        for (var i = 0; i < tree.length; ++i)
            byId[tree[i].id] = tree[i].name
        var names = []
        for (var j = 0; j < ids.length; ++j)
            names.push(byId[ids[j]] !== undefined ? byId[ids[j]] : ("#" + ids[j]))
        return names
    }

    // 函数: 重新读取当前包的元数据（编辑弹窗保存后刷新展示）
    // 输入/输出: 无（读 window.packageId）
    function refresh() {
        if (window.packageId <= 0)
            return
        window.detail = typeEngine.packageDetail(window.packageId)
        window.detailTitle = window.detail.title || "untitled"
        window.tagNames = window.loadTagNames()
    }

    // 函数: 若当前展示的正是该包则刷新（供 AppDialogs 在编辑保存后调用）
    // 输入: id —— 被编辑的包 id；输出: 无
    function refreshIfCurrent(id) {
        if (window.packageId === id)
            window.refresh()
    }

    // 函数: 按类型包声明的包内布局判断是否为整本文本包
    // 输入: form —— 类型标识；输出: bool
    function isTextLayout(form) {
        if (!form)
            return false
        var list = typeEngine.enabledTypes
        for (var i = 0; i < list.length; ++i) {
            if (list[i].form === form)
                return list[i].layoutKind === "wholeBookText"
        }
        return false
    }

    // 函数: 文本包 → 章节行（卷标题分组 + 章标题，含每章字节数）
    // 输入: view —— PackageViewContext；输出: 行数组
    function buildChapterRows(view) {
        var rows = []
        var chapters = view.chapters || []
        if (chapters.length === 0)
            return rows

        // 章文件大小按相对路径索引，避免 O(n²) 查表
        var sizeByPath = ({})
        var files = view.files || []
        for (var f = 0; f < files.length; ++f)
            sizeByPath[files[f].relativePath] = files[f].size

        var lastVolume = -1
        for (var i = 0; i < chapters.length; ++i) {
            var c = chapters[i]
            var vol = c.volume || 0
            if (vol > 0 && vol !== lastVolume) {
                lastVolume = vol
                rows.push({ kind: "volume", title: c.volumeName || ("第 " + vol + " 卷"),
                            number: 0, size: 0 })
            }
            rows.push({ kind: "chapter", title: c.title, number: i + 1,
                        size: sizeByPath[c.file] || 0 })
        }
        return rows
    }

    function openPackage(id) {
        window.packageId = id
        window.detail = typeEngine.packageDetail(id)
        window.detailTitle = window.detail.title || "untitled"
        window.tagNames = window.loadTagNames()
        window.isTextPackage = window.isTextLayout(window.detail.type_form)
        var view = typeEngine.createPackageView(id)
        window.fileList = view ? view.files : []
        window.chapterRows = (view && window.isTextPackage) ? window.buildChapterRows(view) : []
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
                id: metaColumn
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
                        color: "#86b2ff"
                        Text {
                            anchors.centerIn: parent
                            text: "编辑"
                            color: "#ffffff"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: detailEditDialog.openFor(window.packageId)
                        }
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
                    Layout.preferredWidth: Math.round(window.coverWidth)
                    Layout.preferredHeight: Math.round(window.coverHeight)
                    Layout.alignment: Qt.AlignHCenter
                    radius: 6
                    clip: true
                    Image {
                        anchors.fill: parent
                        // 箱体比例已等于封面比例，PreserveAspectFit 保证整图完整可见、不裁切
                        fillMode: Image.PreserveAspectFit
                        source: window.detail.coverUrl || ""
                    }
                }

                // 标题：居中显示在封面下方（不再带「标题」字样）
                Text {
                    Layout.fillWidth: true
                    text: window.detail.title || "untitled"
                    font.pixelSize: 15
                    font.bold: true
                    wrapMode: Text.WrapAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }

                // 标签：位于标题与详细信息之间，圆角蓝色矩形块；无标签时整体隐藏
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    visible: window.tagNames.length > 0
                    spacing: 6
                    Repeater {
                        model: window.tagNames
                        delegate: Rectangle {
                            width: chipLabel.implicitWidth + 16
                            height: 22
                            radius: 6
                            color: "#86b2ff"
                            Text {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: "#ffffff"
                                font.pixelSize: 11
                            }
                        }
                    }
                }

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
                text: window.isTextPackage
                      ? "章节（" + window.chapterRows.length + " 项）"
                      : "包内文件（" + window.fileList.length + " 项）"
                font.pixelSize: 14
                font.bold: true
            }

            // 文本包：按章清单列出（卷标题只分组，不可点击）
            ListView {
                id: chapterList
                visible: window.isTextPackage
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                clip: true
                model: window.chapterRows
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    required property var modelData
                    width: chapterList.width
                    height: modelData.kind === "volume" ? 30 : 28

                    Text {
                        visible: modelData.kind === "volume"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.title
                        font.pixelSize: 12
                        font.bold: true
                        color: "#4a5262"
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        visible: modelData.kind === "chapter"
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 8

                        Text {
                            Layout.preferredWidth: 28
                            horizontalAlignment: Text.AlignRight
                            text: modelData.number + "."
                            font.pixelSize: 12
                            color: "#7a8394"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            text: window.formatBytes(modelData.size)
                            font.pixelSize: 11
                            color: "#9aa3b2"
                        }
                    }
                }
            }

            // 图片包：缩略图带
            GridView {
                visible: !window.isTextPackage
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                clip: true
                // 文本包时给空模型：GridView 即使 visible=false 也会实例化委托，
                // 委托里的 Image 会把 .txt 当图片加载并报解码错误（日志噪声），此处直接不建项
                model: window.isTextPackage ? [] : window.fileList
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
                            // 图片用缩略图；视频无法当图片解码，显示播放符号占位
                            Image {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectFit
                                visible: modelData.media !== "video"
                                source: visible ? (modelData.url || "") : ""
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: modelData.media === "video"
                                text: "▶"
                                font.pixelSize: 26
                                color: "#7a828f"
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

    // 编辑弹窗：本窗口自带实例（弹窗只能渲染在所属窗口的 overlay 内，故不能复用主窗口实例）。
    // 保存成功后重读当前包，使封面/标题/标签/字段与编辑结果一致。
    EditPackageDialog {
        id: detailEditDialog
        onSaved: function (id) { window.refresh() }
        onFailed: function (message) { console.warn("[edit] " + message) }
    }
}
