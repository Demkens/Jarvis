// 核心共享日期输入控件：只读文本框 + 「选择」按钮 + 内嵌月历弹窗。
// 职责：把「日期只能是合法 YYYY-MM-DD」这一约束固化在一处，供筛选弹窗、元数据编辑弹窗等复用
//       （Qt Quick Controls 无内置 DatePicker；类型包不共享本组件——插件与核心禁止互相依赖）。
// 对外属性：text        —— 当前日期文本（YYYY-MM-DD，空串=未设置），可读写；
//           placeholder —— 占位文本（默认 "YYYY-MM-DD"）。
// 对外信号：edited(string value) —— 用户经日历选定某天后发出（值为 YYYY-MM-DD）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: dateField

    // 当前日期文本（读写穿透到内部输入框）
    property alias text: input.text
    // 占位文本
    property string placeholder: "YYYY-MM-DD"

    // 输出: 用户选定的日期（YYYY-MM-DD）
    signal edited(string value)

    spacing: 6

    TextField {
        id: input
        Layout.fillWidth: true
        readOnly: true                       // 仅经日历选择，避免非法格式
        placeholderText: dateField.placeholder
        font.pixelSize: 13
    }

    Button {
        text: "选择"
        onClicked: picker.openAt(input.text)
    }

    // 月历弹窗：仅本控件使用，故内嵌而不单独成文件
    Popup {
        id: picker
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 264
        height: 306
        modal: true
        focus: true
        padding: 10

        // 展示中的年月（month 为 0 基）
        property int viewYear: 2000
        property int viewMonth: 0

        // 函数: "YYYY-MM-DD" → {y, m(0基), d}；缺失部分用今天兜底
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
        // 输入: value —— 日期文本（可空）；输出: 无
        function openAt(value) {
            var p = parseDate(value)
            viewYear = p.y
            viewMonth = p.m
            open()
        }

        // 函数: 生成当月网格单元（月首前置 0 占位，布局时保留空白格）
        // 输出: 数组，元素为 0（占位）或日号
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
                            var p = picker.parseDate(input.text)
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
                                // 直接写文本并发信号：调用方按自己的状态属性接收，
                                // 避免本控件反向改调用方的绑定属性
                                input.text = v
                                dateField.edited(v)
                                picker.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
