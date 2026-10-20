// 活跃热度图（M7，不依赖 Qt Graphs，纯 QML 绘制）。
// 输入: heatmap —— {"total":Int, "max":Int, "cols":Int, "start":"yyyy-MM-dd",
//                   "end":"yyyy-MM-dd", "cells":[{week,dow,date,count}]}，由 StatsService 铺满整张
//        网格输出（cells 列优先，索引 week*7+dow，dow=0 即该列周一，列按周一→周日对齐）。
//        为 null 或 cells 为空时显示空态。
// 职责: 只做渲染——所有日期算术（窗口对齐、每格日期、月份归属）都由核心侧完成，
//       本组件仅按 week/dow 折算像素坐标，避免在 QML 里重算日期（时区/闰月易出错）。
// 交互: 用单个 HoverHandler 覆盖整张网格，再按鼠标坐标反算所在格子——
//       既避免逐格 MouseArea 消费事件挡住整页滚动，也避免相邻格子进出时的提示竞态。
//       悬停提示为自绘 Rectangle：ToolTip 是 Popup，不参与父项布局、定位需跨坐标系换算。
import QtQuick
import QtQuick.Layouts

Item {
    id: chart

    // 输入: 见文件头（StatsService 快照的 heatmap 段）
    property var heatmap: null

    // 单元格间距 / 左侧星期标签列宽 / 顶部月份标签高 / 右下角图例高
    readonly property int cellGap: 2
    readonly property int dowLabelWidth: 18
    readonly property int monthLabelHeight: 14
    readonly property int legendHeight: 20

    readonly property int cols: (heatmap && heatmap.cols) ? heatmap.cols : 0
    readonly property var cells: (heatmap && heatmap.cells) ? heatmap.cells : []
    readonly property int activeTotal: heatmap ? (heatmap.total || 0) : 0

    // 单元格边长：宽度与高度两个方向取较小者，保证既填满卡片又不溢出；下限 6px
    readonly property int sizeByWidth: cols > 0
                                       ? Math.floor((width - dowLabelWidth - cellGap * (cols - 1)) / cols)
                                       : 0
    readonly property int sizeByHeight: Math.floor(
                                           (height - monthLabelHeight - legendHeight - cellGap * 6) / 7)
    readonly property int cellSize: Math.max(6, Math.min(sizeByWidth, sizeByHeight))

    // 函数: 按当日活跃次数取色（浅底卡片上的蓝色 5 档，0 为空白底色）
    // 输入: count —— 当日活跃次数；输出: 颜色字符串
    function colorOf(count) {
        if (count <= 0)
            return "#edf1f7"
        if (count === 1)
            return "#c9dcf7"
        if (count <= 3)
            return "#8fb8ee"
        if (count <= 6)
            return "#4c86dd"
        return "#2a5eb5"
    }

    // 函数: 生成月份标签（每月只在首次出现的列上标一次；跨年后 1 月会再次出现）
    // 输入/输出: 无；输出: [{week, label}]，label 形如「3月」
    function buildMonthLabels() {
        var out = []
        if (chart.cols <= 0)
            return out
        var prev = -1
        for (var w = 0; w < chart.cols; ++w) {
            var idx = w * 7 // 该列第 0 行即周一
            if (idx >= chart.cells.length)
                break
            var month = parseInt(String(chart.cells[idx].date).substring(5, 7), 10)
            if (month !== prev) {
                out.push({ week: w, label: month + "月" })
                prev = month
            }
        }
        return out
    }

    readonly property var monthLabels: chart.buildMonthLabels()

    // 网格宿主：格子、悬停提示共用一个坐标系，整体在可用宽度内水平居中（左侧让出星期标签列）
    Item {
        id: gridHost

        width: chart.cols * chart.cellSize + Math.max(0, chart.cols - 1) * chart.cellGap
        height: 7 * chart.cellSize + 6 * chart.cellGap
        x: chart.dowLabelWidth + Math.max(0, (chart.width - chart.dowLabelWidth - width) / 2)
        y: chart.monthLabelHeight

        // 函数: 按当前悬停坐标反算所在格子并刷新提示框位置与文本
        // 输入/输出: 无（读 gridHover.point / chart.cells）
        function refreshTip() {
            // 鼠标已移出网格：直接收起，避免残留提示
            if (!gridHover.hovered) {
                hoverTip.visible = false
                return
            }
            var step = chart.cellSize + chart.cellGap
            if (step <= 0)
                return
            var week = Math.floor(gridHover.point.position.x / step)
            var dow = Math.floor(gridHover.point.position.y / step)
            if (week < 0 || week >= chart.cols || dow < 0 || dow > 6) {
                hoverTip.visible = false
                return
            }
            var idx = week * 7 + dow
            if (idx < 0 || idx >= chart.cells.length)
                return

            var cell = chart.cells[idx]
            hoverTip.tipText = cell.date + " · " + cell.count + " 次活跃"
            hoverTip.x = Math.max(0, Math.min(gridHost.width - hoverTip.width,
                                              week * step + chart.cellSize / 2
                                              - hoverTip.width / 2))
            // 默认浮在格子上方；顶部空间不足时改到下方
            hoverTip.y = dow * step - hoverTip.height - 4
            if (hoverTip.y < 0)
                hoverTip.y = dow * step + chart.cellSize + 4
            hoverTip.visible = true
        }

        // 整张网格共用一个悬停处理器；HoverHandler 不消费鼠标事件，滚轮照常传给整页 Flickable
        HoverHandler {
            id: gridHover

            onHoveredChanged: {
                if (hovered) {
                    // 进入时立即按当前坐标出提示（point 可能先于 hovered 更新，故不依赖顺序）
                    gridHost.refreshTip()
                } else {
                    hoverTip.visible = false
                }
            }
            onPointChanged: gridHost.refreshTip()
        }

        Repeater {
            model: chart.cells

            delegate: Rectangle {
                required property var modelData

                x: modelData.week * (chart.cellSize + chart.cellGap)
                y: modelData.dow * (chart.cellSize + chart.cellGap)
                width: chart.cellSize
                height: chart.cellSize
                radius: 2
                color: chart.colorOf(modelData.count)
            }
        }

        // 悬停提示（自绘，固定宽度以便居中定位无需等文本宽度更新）
        Rectangle {
            id: hoverTip
            property string tipText: ""

            visible: false
            z: 100
            width: 132
            height: 20
            radius: 3
            color: "#2c3442"

            Text {
                anchors.centerIn: parent
                text: hoverTip.tipText
                color: "#ffffff"
                font.pixelSize: 11
            }
        }
    }

    // 左侧星期标签：只标 一/三/五 三行，避免拥挤
    Repeater {
        model: [{ d: 0, label: "一" }, { d: 2, label: "三" }, { d: 4, label: "五" }]

        delegate: Text {
            required property var modelData

            x: 0
            width: chart.dowLabelWidth - 4
            horizontalAlignment: Text.AlignRight
            y: chart.monthLabelHeight + modelData.d * (chart.cellSize + chart.cellGap)
               + (chart.cellSize - height) / 2
            text: modelData.label
            color: "#8a94a6"
            font.pixelSize: 10
        }
    }

    // 顶部月份标签
    Repeater {
        model: chart.monthLabels

        delegate: Text {
            required property var modelData

            x: gridHost.x + modelData.week * (chart.cellSize + chart.cellGap)
            y: 0
            text: modelData.label
            color: "#8a94a6"
            font.pixelSize: 10
        }
    }

    // 右下角色阶图例
    RowLayout {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 3

        Text {
            text: "少"
            color: "#8a94a6"
            font.pixelSize: 10
            Layout.alignment: Qt.AlignVCenter
        }
        Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.alignment: Qt.AlignVCenter; radius: 2; color: "#edf1f7" }
        Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.alignment: Qt.AlignVCenter; radius: 2; color: "#c9dcf7" }
        Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.alignment: Qt.AlignVCenter; radius: 2; color: "#8fb8ee" }
        Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.alignment: Qt.AlignVCenter; radius: 2; color: "#4c86dd" }
        Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.alignment: Qt.AlignVCenter; radius: 2; color: "#2a5eb5" }
        Text {
            text: "多"
            color: "#8a94a6"
            font.pixelSize: 10
            Layout.alignment: Qt.AlignVCenter
        }
    }

    // 无数据（未取到快照）或窗口内无活跃时的文字提示
    Text {
        anchors.centerIn: parent
        visible: chart.cells.length === 0 || chart.activeTotal === 0
        text: chart.cells.length === 0 ? "暂无数据" : "近 12 个月暂无活跃记录"
        font.pixelSize: 12
        color: "#8a94a6"
    }
}
