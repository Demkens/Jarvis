// 资源页统一筛选弹窗（右上角「筛选」按钮唤起）。
// 职责：集中承载资源页的全部筛选维度（一期：类型 / 创作日期区间 / 入库时间区间；
//       M6：标签树 + AND/OR 组合开关；后续：创作者），以「应用」一次性提交，
//       避免各维度散落顶栏形成多入口。
// 标签筛选语义：选中某标签即命中「打了该标签或其全部子孙」的数据包（子孙含、父不含）；
//       多标签时由开关决定组合方式——关=OR（命中任一）、开=AND（须同时命中全部）。
//       筛选态的标签树不作反链约束（enforceAntichain=false），允许自由多选。
// 数据流：本弹窗自身不持有筛选状态——openWith() 按当前生效值填充各输入控件，
//       commit() 再从控件读取并打包输出；资源页是筛选状态的唯一持有者。
// 对外属性：typeModel —— 类型下拉模型（[{form, displayName}]，首位为「全部类型」）；
// 对外方法：openWith(filters) —— 以当前生效的筛选值初始化后弹出；
// 对外信号：applied(var filters) —— 点「应用」时发出，键与 ResourcePage.filters 一致。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../include"

Popup {
    id: dialog

    // 类型下拉模型（由资源页注入：首位「全部类型」+ 已启用类型）
    property var typeModel: []

    // 区间校验提示（空=无错）
    property string errorText: ""

    // 输出: 应用筛选（filters 含 form/creationFrom/creationTo/createdFrom/createdTo/tagIds/tagMatchAll）
    signal applied(var filters)

    width: 440
    height: 620
    modal: true
    focus: true
    padding: 16
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    // 函数: 在注入模型里找 form 对应的下标（找不到→0，即「全部类型」）
    // 输入: form —— 类型标识（空=全部）；输出: 下标
    function indexOfForm(form) {
        for (var i = 0; i < typeModel.length; ++i) {
            if (typeModel[i].form === form)
                return i
        }
        return 0
    }

    // 函数: 以现有筛选值初始化各输入控件并弹出
    // 输入: filters —— 当前生效筛选（可为空对象）；输出: 无
    function openWith(filters) {
        var f = filters || ({})
        // 直接写控件（不用声明式绑定：DateField 选定日期时会写自身文本，绑定会被打断）
        typeCombo.currentIndex = indexOfForm(f.form || "")
        creationFromField.text = f.creationFrom || ""
        creationToField.text = f.creationTo || ""
        createdFromField.text = f.createdFrom || ""
        createdToField.text = f.createdTo || ""
        // 标签：整体替换勾选集合（筛选态不强制反链，允许嵌套多选）
        tagTree.checkedIds = f.tagIds || []
        matchAllSwitch.checked = !!f.tagMatchAll
        errorText = ""
        open()
    }

    // 函数: 清空弹窗内全部条件（不关闭、不立即查询，需再点「应用」）
    // 输入/输出: 无
    function clearAll() {
        typeCombo.currentIndex = 0
        creationFromField.text = ""
        creationToField.text = ""
        createdFromField.text = ""
        createdToField.text = ""
        tagTree.checkedIds = []
        matchAllSwitch.checked = false
        errorText = ""
    }

    // 函数: 校验 + 打包输出（区间起点晚于终点时拦截）
    // 输入/输出: 无；通过→发 applied 并关闭，不通过→写 errorText 留在弹窗内
    function commit() {
        var creationFrom = creationFromField.text
        var creationTo = creationToField.text
        if (creationFrom && creationTo && creationFrom > creationTo) {
            errorText = "创作日期区间：起始日期不能晚于结束日期"
            return
        }
        var createdFrom = createdFromField.text
        var createdTo = createdToField.text
        if (createdFrom && createdTo && createdFrom > createdTo) {
            errorText = "入库时间区间：起始日期不能晚于结束日期"
            return
        }
        errorText = ""

        var form = ""
        if (typeCombo.currentIndex >= 0 && typeCombo.currentIndex < typeModel.length)
            form = typeModel[typeCombo.currentIndex].form

        dialog.applied({
            form: form,
            creationFrom: creationFrom,
            creationTo: creationTo,
            createdFrom: createdFrom,
            createdTo: createdTo,
            // 标签 id 数组按模型先序拷贝（避免与树内部数组共享引用）
            tagIds: tagTree.checkedIds.slice(0),
            tagMatchAll: matchAllSwitch.checked
        })
        dialog.close()
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "筛选"
            font.pixelSize: 15
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            // ---- 类型 ----
            Text { text: "类型"; font.pixelSize: 12 }
            ComboBox {
                id: typeCombo
                Layout.fillWidth: true
                Layout.preferredWidth: 260
                model: dialog.typeModel
                textRole: "displayName"
            }

            // ---- 创作日期区间（作品日期，闭区间）----
            Text { text: "创作日期"; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                DateField {
                    id: creationFromField
                    Layout.fillWidth: true
                    placeholder: "起始（含）"
                }
                Text { text: "至"; font.pixelSize: 12 }
                DateField {
                    id: creationToField
                    Layout.fillWidth: true
                    placeholder: "结束（含）"
                }
            }

            // ---- 入库时间区间（导入时刻，闭区间）----
            Text { text: "入库时间"; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                DateField {
                    id: createdFromField
                    Layout.fillWidth: true
                    placeholder: "起始（含）"
                }
                Text { text: "至"; font.pixelSize: 12 }
                DateField {
                    id: createdToField
                    Layout.fillWidth: true
                    placeholder: "结束（含）"
                }
            }
        }

        // ---- 标签（M6：选中即命中该标签或其全部子孙；筛选态允许嵌套多选）----
        Text {
            Layout.fillWidth: true
            text: "标签"
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            color: "#f7f9fc"
            border.color: "#d5dbe6"
            border.width: 1
            radius: 4

            TagTree {
                id: tagTree
                anchors.fill: parent
                anchors.margins: 4
                enforceAntichain: false
                tagModel: typeEngine.tagTree
            }

            Text {
                anchors.centerIn: parent
                visible: (typeEngine.tagTree || []).length === 0
                text: "暂无标签（可在顶栏「工具 → 标签管理…」中创建）"
                color: "#9aa3b2"
                font.pixelSize: 11
            }
        }

        // 多标签组合方式：关=OR（命中任一）；开=AND（须同时命中全部）
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "多标签组合"
                font.pixelSize: 12
            }
            Switch {
                id: matchAllSwitch
                text: matchAllSwitch.checked ? "AND（须全部命中）" : "OR（命中任一）"
                font.pixelSize: 12
            }
        }

        Text {
            Layout.fillWidth: true
            visible: dialog.errorText !== ""
            text: dialog.errorText
            color: "#c0392b"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
        }

        Text {
            Layout.fillWidth: true
            text: "留空即该维度不限；标签命中含其全部子孙（不含父）。"
            color: "#7a7a7a"
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
        }

        // ---- 操作 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 30
                radius: 4
                color: "#f0f0f0"
                Text {
                    anchors.centerIn: parent
                    text: "重置"
                    color: "#2b2b2b"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.clearAll()
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 30
                radius: 4
                color: "#e6eaf2"
                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    color: "#2b2b2b"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.close()
                }
            }
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 30
                radius: 4
                color: "#86b2ff"
                Text {
                    anchors.centerIn: parent
                    text: "应用"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.commit()
                }
            }
        }
    }
}
