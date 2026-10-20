// 按类型分布环形图（M7，Qt Graphs 实现）。
// 输入: model —— [{name, count}]（name 为类型中文显示名）；空数组时显示「暂无数据」占位。
// 说明: Qt Graphs 无内置图例（官方迁移文档把 "Titles and legends" 明确列为缺失项），
//       故扇区与图例共用同一份 _entries（名称/数量/颜色），图例由本组件在 GraphsView 之外自绘；
//       类型数超过调色板容量时把长尾合并为「其他」，避免图例挤爆。
// 注意: GraphsView 的默认属性是 seriesList（子元素一律当作序列），故图例与空态必须放在其外侧。
import QtQuick
import QtGraphs

Item {
    id: chart

    // 输入: [{name, count}]
    property var model: []

    // 扇区配色（浅底可读的顺序色板；末色留给「其他」）
    readonly property var _palette: ["#3d6ecd", "#4caf7d", "#e5a05f", "#c06c84",
                                     "#6c9fd6", "#8e7cc3", "#4aa8a0", "#c9a227"]

    readonly property bool hasData: model && model.length > 0
    // 是否需要合并长尾
    readonly property bool _merged: (model ? model.length : 0) > _palette.length
    // 逐项上色的扇区数（合并时留出末色给「其他」）
    readonly property int _headCount: _merged ? _palette.length - 1 : (model ? model.length : 0)
    // 扇区与图例共用的条目表：[{name, count, color}]
    readonly property var _entries: chart.buildEntries()

    // 函数: 由 model 计算条目表（含长尾合并与逐项配色）
    // 输入/输出: 无（读 chart.model / chart._palette）；输出: [{name, count, color}]
    function buildEntries() {
        var out = []
        if (!hasData)
            return out
        for (var i = 0; i < chart._headCount; ++i)
            out.push({
                          name: model[i].name,
                          count: Number(model[i].count),
                          color: chart._palette[i % chart._palette.length]
                      })
        if (!chart._merged)
            return out
        var rest = 0
        for (var j = chart._headCount; j < model.length; ++j)
            rest += Number(model[j].count)
        if (rest > 0)
            out.push({ name: "其他", count: rest, color: "#9aa3b2" })
        return out
    }

    // 函数: 按当前条目表重建扇区（PieSeries.append 返回扇区对象，可继续设置颜色）
    // 输入/输出: 无
    function rebuild() {
        pie.clear()
        for (var i = 0; i < chart._entries.length; ++i) {
            var entry = chart._entries[i]
            var slice = pie.append(entry.name, entry.count)
            slice.color = entry.color
            slice.labelVisible = false // 名称由图例承载，扇区上不再重复标注
        }
    }

    GraphsView {
        id: view
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: legend.top
        anchors.bottomMargin: 4

        theme: GraphsTheme {
            // 显式指定浅色方案与自配色，避免随桌面深浅色主题漂移
            colorScheme: GraphsTheme.ColorScheme.Light
            theme: GraphsTheme.Theme.UserDefined
            backgroundColor: "transparent"
            backgroundVisible: false
            plotAreaBackgroundVisible: false
            labelTextColor: "#5c6472"
            labelBackgroundVisible: false
            labelBorderVisible: false
            // 强制纯色：扇区颜色由 slice.color 逐项指定（默认的渐变方案会覆盖对象自身颜色）。
            // 注意：不可写 baseColors —— 它在 qmltypes 中是只读常量列表（类型 QQuickGraphsColor），
            // 赋值会被判为非法颜色并刷出 "Color is invalid, use Color" 警告。
            colorStyle: GraphsTheme.ColorStyle.Uniform
        }

        PieSeries {
            id: pie
            pieSize: 0.95
            holeSize: 0.4
        }
    }

    // 自绘图例：每行两项（色块 + 名称 + 数量），配色与扇区同源
    Flow {
        id: legend
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 4

        Repeater {
            model: chart._entries

            Item {
                required property var modelData
                width: Math.floor((legend.width - legend.spacing) / 2)
                height: 18

                Rectangle {
                    id: chip
                    x: 0
                    width: 10
                    height: 10
                    radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.color
                }

                Text {
                    anchors.left: chip.right
                    anchors.leftMargin: 5
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: modelData.name + "  " + modelData.count
                    font.pixelSize: 11
                    color: "#5c6472"
                }
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

    // 模型整体替换（页面刷新时赋值）→ 重建扇区
    onModelChanged: chart.rebuild()
    Component.onCompleted: chart.rebuild()
}
