// 数据统计页（M7）。
// 职责：以只读方式呈现当前库的整体规模与各维度分布，不参与任何写操作、不与资源页联动。
// 数据来源：statsService（核心只读聚合门面）。
//   - 基线快照（总数 / 类型数 / 创作者数 / 按类型 / 按创作者 / 按创作月份）：
//     带缓存，核心侧事件只置脏；本页仅在「回到前台」或「点刷新」时取值，
//     避免批量导入期间反复全表聚合造成卡顿。
//   - 标签计量：随选中标签即时计算，取数语义与资源页筛选完全一致
//     （命中「该标签或其全部子孙」的数据包；多标签各计一次，占比之和可超 100%）。
// 状态持有：本页是「已选标签集合」的唯一持有者，弹窗只负责编辑并回传。
// 对外属性：foreground —— 是否位于前台（由 Main.qml 绑定），false→true 时惰性重算。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../include"
import "../dialogs"

Item {
    id: page

    // 输入: 是否位于前台（Main.qml 注入；用于惰性重算）
    property bool foreground: false

    // 基线快照（statsService.snapshot() 的返回，空对象表示尚未取过）
    property var snap: ({})
    // 是否已成功取过一次快照（用于区分"尚未加载"与"确实无数据"，避免首帧闪空态）
    property bool loaded: false
    // 已选标签 id 数组（本页唯一持有；标签计量维度的输入）
    property var selectedTagIds: []
    // 标签计量结果项 [{tagId,name,count,percent}]
    property var tagStatItems: []

    readonly property bool hasLibrary: libraryService.currentName !== ""
    readonly property bool hasData: hasLibrary && Number(page.snap.total || 0) > 0

    // 函数: 取基线快照并按当前选中标签重算标签计量
    // 输入: force —— true 表示忽略核心缓存强制重算（刷新按钮）；false 走缓存
    // 输出: 无（结果写入 snap / tagStatItems）
    function reload(force) {
        if (!hasLibrary) {
            page.snap = {}
            page.loaded = false
            page.tagStatItems = []
            return
        }
        var result = force ? statsService.refresh() : statsService.snapshot()
        if (!result.ok) {
            console.warn("[stats] 统计失败：" + (result.message || "未知错误"))
            page.snap = {}
            page.loaded = false
            return
        }
        page.snap = result
        page.loaded = true
        page.reloadTagStats()
    }

    // 函数: 按当前选中标签重算标签计量（未选标签则清空结果，由界面显示引导态）
    // 输入/输出: 无
    function reloadTagStats() {
        if (!hasLibrary || page.selectedTagIds.length === 0) {
            page.tagStatItems = []
            return
        }
        var result = statsService.tagStats(page.selectedTagIds)
        if (!result.ok) {
            console.warn("[stats] 标签计量失败：" + (result.message || "未知错误"))
            page.tagStatItems = []
            return
        }
        page.tagStatItems = result.items || []
    }

    // 函数: 查标签显示名（用于已选标签 chips）
    // 输入: id —— 标签 id；输出: 标签名（已删除的标签返回「（已删除）」）
    function tagName(id) {
        var tree = typeEngine.tagTree || []
        for (var i = 0; i < tree.length; ++i) {
            if (tree[i].id === id)
                return tree[i].name
        }
        return "（已删除）"
    }

    // 回到前台时才重算（核心侧事件已把快照置脏）
    onForegroundChanged: {
        if (foreground)
            page.reload(false)
    }

    Component.onCompleted: {
        if (page.foreground)
            page.reload(false)
    }

    // 库切换（打开/关闭库）：前台时立即重算并清空标签选择
    Connections {
        target: libraryService
        function onOpenChanged() {
            page.selectedTagIds = []
            page.tagStatItems = []
            if (page.foreground)
                page.reload(false)
        }
    }

    // 页头
    RowLayout {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 16
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        Text {
            text: "数据统计"
            font.pixelSize: 16
            font.bold: true
        }

        Item { Layout.fillWidth: true }

        // 数据已过期提示：前台停留期间发生导入/编辑时出现，提示用户点刷新
        Text {
            visible: page.hasLibrary && statsService.stale
            text: "数据已更新，可刷新"
            font.pixelSize: 11
            color: "#c0752a"
        }

        Rectangle {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 30
            radius: 4
            color: "#86b2ff"
            Text {
                anchors.centerIn: parent
                text: "刷新"
                color: "#ffffff"
                font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                onClicked: page.reload(true)
            }
        }
    }

    // 空态：未打开库
    Text {
        anchors.centerIn: parent
        visible: !page.hasLibrary
        text: "尚未打开库"
        font.pixelSize: 16
    }

    // 空态：有库但无数据包（已加载完成才判定，避免首帧闪空态）
    Text {
        anchors.centerIn: parent
        visible: page.hasLibrary && page.loaded && !page.hasData
        text: "库无文件，待导入"
        font.pixelSize: 16
    }

    // 正文（有库且有数据时）
    Flickable {
        id: content
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 12
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 16
        visible: page.hasData
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: body.height

        ColumnLayout {
            id: body
            width: content.width
            spacing: 14

            // ---- 规模数字卡片 ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                StatCard {
                    Layout.fillWidth: true
                    title: "数据包总数"
                    value: String(page.snap.total || 0)
                }
                StatCard {
                    Layout.fillWidth: true
                    title: "类型数"
                    value: String(page.snap.typeCount || 0)
                    hint: "库内已登记类型"
                }
                StatCard {
                    Layout.fillWidth: true
                    title: "创作者数"
                    value: String(page.snap.creatorCount || 0)
                    hint: "库内已登记创作者"
                }
            }

            // ---- 按类型分布（饼图）----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                color: "#f7f9fc"
                border.color: "#d5dbe6"
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text { text: "按类型分布"; font.pixelSize: 13; font.bold: true }

                    TypePieChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // 图表无需交互：禁用输入以免吞掉滚轮/拖拽，保证整页可滚动
                        enabled: false
                        model: page.snap.byType || []
                    }
                }
            }

            // ---- 按创作者分布（横向条形：Top 10 + 其他）----
            Rectangle {
                Layout.fillWidth: true
                // 高度随条目数增长（每条约 26px），保证类别标签不重叠
                Layout.preferredHeight: Math.max(200, 56 + (page.snap.byCreator || []).length * 26)
                color: "#f7f9fc"
                border.color: "#d5dbe6"
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text {
                        text: "按创作者分布（Top 10）"
                        font.pixelSize: 13
                        font.bold: true
                    }

                    CreatorBarChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        enabled: false
                        model: page.snap.byCreator || []
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "联合创作的数据包会在多个创作者名下各计一次，故各条之和可大于数据包总数。"
                        font.pixelSize: 10
                        color: "#9aa3b2"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ---- 按创作月份分布（折线）----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                color: "#f7f9fc"
                border.color: "#d5dbe6"
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text { text: "按创作月份分布"; font.pixelSize: 13; font.bold: true }

                    MonthLineChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        enabled: false
                        model: page.snap.byMonth || []
                    }
                }
            }

            // ---- 标签计量（选中标签 → 命中包数）----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 340
                color: "#f7f9fc"
                border.color: "#d5dbe6"
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text { text: "标签计量"; font.pixelSize: 13; font.bold: true }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: 88
                            Layout.preferredHeight: 28
                            radius: 4
                            color: "#f0f0f0"
                            Text {
                                anchors.centerIn: parent
                                text: "选择标签…"
                                font.pixelSize: 12
                                color: "#2b2b2b"
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: tagPickDialog.openWith(page.selectedTagIds)
                            }
                        }
                    }

                    // 已选标签 chips（自动换行；点 × 移除该标签）
                    Flow {
                        Layout.fillWidth: true
                        visible: page.selectedTagIds.length > 0
                        spacing: 6

                        Repeater {
                            model: page.selectedTagIds

                            delegate: Rectangle {
                                required property var modelData

                                width: chipLabel.width + 30
                                height: 22
                                radius: 11
                                color: "#e6eaf2"

                                Text {
                                    id: chipLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    text: page.tagName(modelData)
                                    font.pixelSize: 11
                                    color: "#2b2b2b"
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    text: "×"
                                    font.pixelSize: 13
                                    color: "#5c6472"
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            page.selectedTagIds = page.selectedTagIds.filter(
                                                        function (v) { return v !== modelData })
                                            page.reloadTagStats()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 引导态：未选标签
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.selectedTagIds.length === 0
                        text: "请选择标签，查看打了该标签（或其全部子孙）的数据包数量。"
                        font.pixelSize: 12
                        color: "#9aa3b2"
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // 结果图
                    TagBarChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.selectedTagIds.length > 0
                        enabled: false
                        items: page.tagStatItems
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: page.selectedTagIds.length > 0
                        text: "每个选中标签各计一次，多标签时各柱之和可大于数据包总数（占比之和可超 100%）。"
                        font.pixelSize: 10
                        color: "#9aa3b2"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // 底部留白（避免最后一块贴住视口下沿）
            Item { Layout.preferredHeight: 2 }
        }
    }

    // 标签选择弹窗：仅编辑「已选标签集合」，确定后回传并即时重算
    TagPickDialog {
        id: tagPickDialog
        onApplied: function (tagIds) {
            page.selectedTagIds = tagIds
            page.reloadTagStats()
        }
    }
}