// 标签计量条形图（M7，Qt Graphs 实现）。
// 输入: items —— [{tagId, name, count, percent}]（核心按选中标签逐个统计的命中数与占比）；
//       空数组时显示「暂无数据」。
// 说明: X 轴 = 选中标签（类别轴），Y 轴 = 命中该标签（或其全部子孙）的数据包数；
//       多标签各自计数，故各柱之和可大于全库总数（占比之和可超 100%，页面另有文字注明）。
//       Qt Graphs 不为数值轴自动计算范围，故 Y 轴上界由 items 最大值加余量后显式给定。
import QtQuick
import QtGraphs

Item {
    id: chart

    // 输入: [{tagId, name, count, percent}]
    property var items: []

    readonly property bool hasData: items && items.length > 0
    readonly property var categories: hasData ? items.map(function (item) { return item.name }) : []
    readonly property var values: hasData ? items.map(function (item) { return Number(item.count) }) : []
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
        marginTop: 8
        marginLeft: 40
        marginRight: 12
        // 标签名通常较长且斜排，底部留足高度
        marginBottom: 44

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
            seriesColors: ["#6c9fd6"]
        }

        axisX: BarCategoryAxis {
            categories: chart.categories
            labelsAngle: -30 // 标签名通常较长，斜排避免相互遮挡
            lineVisible: false
            gridVisible: false
        }
        axisY: ValueAxis {
            min: 0
            max: chart.valueMax
            labelFormat: "%.0f"
        }

        BarSeries {
            BarSet {
                values: chart.values
                color: "#6c9fd6"
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
