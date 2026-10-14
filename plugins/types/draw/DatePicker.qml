// draw 类型包 · 轻量日期选择弹窗（月历网格）。
// 用途：wizard 的「创作日期」选择控件。Qt Quick Controls 无内置 DatePicker，故自建。
// 属性 date：当前值，格式 YYYY-MM-DD（空串则定位到今天）；
// 信号 dateSelected(string value)：用户点选某天后发出（值为 YYYY-MM-DD）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: picker

    // 当前值（YYYY-MM-DD）
    property string date: ""
    // 展示中的年月（month 为 0 基）
    property int viewYear: 2000
    property int viewMonth: 0

    // 输出: 用户选中的日期
    signal dateSelected(string value)

    width: 264
    height: 306
    modal: true
    focus: true
    padding: 10

    // 函数: 把 "YYYY-MM-DD" 解析为 {y, m(0基), d}；缺失部分用今天兜底
    function parseDate(s) {
        var now = new Date()
        var parts = String(s).split("-")
        var y = parseInt(parts[0])
        var m = parseInt(parts[1])
        var d = parseInt(parts[2])
        if (isNaN(y))
            y = now.getFullYear()
        if (isNaN(m))
            m = now.getMonth() + 1
        if (isNaN(d))
            d = now.getDate()
        return {y: y, m: m - 1, d: d}
    }

    // 函数: 以给定值定位所在月份并弹出
    function openAt(value) {
        var p = parseDate(value)
        viewYear = p.y
        viewMonth = p.m
        open()
    }

    // 函数: 生成当月网格单元（月首前置空位以 0 占位，布局时隐藏）
    function cells() {
        var lead = new Date(viewYear, viewMonth, 1).getDay()      // 0=周日
        var days = new Date(viewYear, viewMonth + 1, 0).getDate()
        var out = []
        for (var i = 0; i < lead; ++i)
            out.push(0)
        for (var d = 1; d <= days; ++d)
            out.push(d)
        return out
    }

    function pad2(n) { return n < 10 ? "0" + n : String(n) }

    contentItem: ColumnLayout {
        spacing: 6

        // 月份切换
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Button {
                text: "‹"
                flat: true
                onClicked: {
                    if (--picker.viewMonth < 0) {
                        picker.viewMonth = 11
                        --picker.viewYear
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: picker.viewYear + " 年 " + (picker.viewMonth + 1) + " 月"
                font.pixelSize: 13
            }
            Button {
                text: "›"
                flat: true
                onClicked: {
                    if (++picker.viewMonth > 11) {
                        picker.viewMonth = 0
                        ++picker.viewYear
                    }
                }
            }
        }

        // 星期表头
        GridLayout {
            Layout.fillWidth: true
            columns: 7
            rowSpacing: 2
            columnSpacing: 2
            Repeater {
                model: ["日", "一", "二", "三", "四", "五", "六"]
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData
                    font.pixelSize: 11
                }
            }
        }

        // 日网格（0 为月首前置占位：必须保留可见以占格，否则日期与星期错列——
        // QtQuick Layouts 会跳过 invisible 子项、不为其保留单元）
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 7
            rowSpacing: 2
            columnSpacing: 2
            Repeater {
                model: picker.cells()
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: 4
                    color: {
                        if (modelData === 0)
                            return "transparent"
                        var p = picker.parseDate(picker.date)
                        var sel = (p.y === picker.viewYear && p.m === picker.viewMonth
                                   && p.d === modelData)
                        return sel ? "#3d6ecd" : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: modelData === 0 ? "" : modelData
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: modelData !== 0
                        onClicked: {
                            var v = picker.viewYear + "-" + picker.pad2(picker.viewMonth + 1)
                                    + "-" + picker.pad2(modelData)
                            picker.date = v
                            picker.dateSelected(v)
                            picker.close()
                        }
                    }
                }
            }
        }
    }
}
