// 按创作月份分布折线图（M7，Qt Graphs 实现）。
// 输入: model —— [{month, count}]（month 为 YYYY-MM，已按月份升序）；空数组时显示「暂无数据」。
// 说明: creation_date 恒为 YYYY-MM-DD，故各月计数之和等于数据包总数；
//       X 轴用类别轴承载月份字符串（等距排列、无空档），折线点位 x 取类别下标（0,1,2…）；
//       Qt Graphs 不为数值轴自动计算范围，Y 轴上界由模型最大值加余量后显式给定。
import QtQuick
import QtGraphs

Item {
    id: chart

    // 输入: [{month, count}]
    property var model: []

    readonly property bool hasData: model && model.length > 0
    readonly property var categories: hasData ? model.map(function (item) { return item.month }) : []
    readonly property real valueMax: chart.computeValueMax()

    // 函数: 计算数值轴上界（空数据时取 1，保证轴区间合法）
    // 输入/输出: 无；输出: 轴最大值
    function computeValueMax() {
        var max = 1
        if (!hasData)
            return max
        for (var i = 0; i < model.length; ++i)
            max = Math.max(max, Number(model[i].count))
        return Math.ceil(max * 1.15)
    }

    // 函数: 按当前 model 重建折线点位
    // 输入/输出: 无
    function rebuild() {
        line.clear()
        if (!chart.hasData)
            return
        for (var i = 0; i < model.length; ++i)
            line.append(i, Number(model[i].count))
    }

    GraphsView {
        anchors.fill: parent
        marginTop: 8
        marginLeft: 40
        marginRight: 12
        // 月份标签斜排，底部需留足高度
        marginBottom: 40

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

        axisX: BarCategoryAxis {
            categories: chart.categories
            labelsAngle: -45 // 月份标签斜排，避免相邻标签互相遮挡
            lineVisible: false
            gridVisible: false
        }
        axisY: ValueAxis {
            min: 0
            max: chart.valueMax
            labelFormat: "%.0f"
        }

        LineSeries {
            id: line
            width: 2
            color: "#3d6ecd"
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

    // 模型整体替换（页面刷新时赋值）→ 重建点位
    onModelChanged: chart.rebuild()
    Component.onCompleted: chart.rebuild()
}
