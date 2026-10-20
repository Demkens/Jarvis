// 按创作者分布横向条形图（M7，Qt Graphs 实现）。
// 输入: model —— [{name, count, others}]（核心侧已归并为 Top10 + 可选的「其他」项）；
//       空数组时显示「暂无数据」占位。
// 说明: 采用横向条形（类别落在竖直方向），避免中文创作者名过长互相遮挡。
//       Qt Graphs 已无 HorizontalBarSeries，横向条形由 GraphsView.orientation = Qt.Horizontal 实现——
//       官方语义：此时 axisX 渲染为竖直方向、axisY 渲染为水平方向（该属性目前仅作用于 BarSeries）。
//       Qt Graphs 不为数值轴自动计算范围，故上界由模型最大值加余量后显式给定。
import QtQuick
import QtGraphs

Item {
    id: chart

    // 输入: [{name, count, others}]
    property var model: []

    readonly property bool hasData: model && model.length > 0
    readonly property var categories: hasData ? model.map(function (item) { return item.name }) : []
    readonly property var values: hasData ? model.map(function (item) { return Number(item.count) }) : []
    // 数值轴上界：最大值再留 15% 余量，避免最长条形顶到边框
    readonly property real valueMax: chart.computeValueMax()

    // 函数: 计算数值轴上界（空数据时取 1，保证轴区间合法）
    // 输入/输出: 无（读 chart.values）；输出: 轴最大值
    function computeValueMax() {
        var max = 1
        for (var i = 0; i < chart.values.length; ++i)
            max = Math.max(max, chart.values[i])
        return Math.ceil(max * 1.15)
    }

    GraphsView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        // 左侧留出类别标签宽度（中文创作者名可能较长）；底部留出数值轴标签高度
        marginLeft: 84
        marginRight: 16
        marginTop: 8
        marginBottom: 26

        theme: GraphsTheme {
            colorScheme: GraphsTheme.ColorScheme.Light
            theme: GraphsTheme.Theme.UserDefined
            backgroundColor: "transparent"
            backgroundVisible: false
            plotAreaBackgroundVisible: false
            gridVisible: true
            grid.mainColor: "#e8ecf3"
            labelTextColor: "#5c6472"
            labelBackgroundVisible: false
            labelBorderVisible: false
            axisX.mainColor: "#d5dbe6"
            axisY.mainColor: "#d5dbe6"
            seriesColors: ["#3d6ecd"]
        }

        // 横向模式下：axisX 竖直渲染（承载创作者类别），axisY 水平渲染（承载数量）
        axisX: BarCategoryAxis {
            categories: chart.categories
            lineVisible: false
            gridVisible: false
        }
        axisY: ValueAxis {
            min: 0
            max: chart.valueMax
            labelFormat: "%.0f"
        }

        BarSeries {
            barWidth: 0.6

            BarSet {
                values: chart.values
                color: "#3d6ecd"
            }
        }
    }

    // 空态占位
    Text {
        anchors.centerIn: parent
        visible: !chart.hasData
        text: "暂无数据"
        font.pixelSize: 12
        color: "#9aa3b2"
    }
}
