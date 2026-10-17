// 包元数据编辑弹窗（一期验收：单击卡片可直接编辑元数据）。
// 职责：集中编辑单个数据包的核心元数据——标题 / 创作者（可多作者） / 评分 / 创作日期 / 标签；
//       所有写入都经 TypeEngine 门面（类型包与 QML 均不得直碰数据库）。
// 打标签约束：标签树以 enforceAntichain=true 呈现——已勾选标签的祖先灰显不可勾，
//       勾选子节点会自动移出已勾选的祖先，保证提交的集合恒为反链（服务层亦会二次校验）。
// 对外属性：packageId —— 目标包 id；
//           readOnlyFields —— 只读展示行（类型/大小/入库时间等，由 openFor 填充）；
// 对外方法：openFor(id) —— 读库填充表单后弹出；
// 对外信号：saved(int packageId) —— 全部写入成功并关闭后发出；
//           failed(string message) —— 写入失败时发出（弹窗保持打开，便于修正后重试）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../include"

Popup {
    id: dialog

    // 目标包 id
    property int packageId: 0
    // 只读展示：类型显示名
    property string typeName: "—"
    // 只读展示：入库时间（YYYY-MM-DD）
    property string createdText: "—"

    // 输出: 写入成功
    signal saved(int packageId)
    // 输出: 写入失败原因
    signal failed(string message)

    // 写库失败提示（空=无错）
    property string errorText: ""

    width: 460
    height: 640
    modal: true
    focus: true
    padding: 16
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    // 函数: 类型 form → 显示名（取自当前库已启用类型）
    // 输入: form —— 类型标识；输出: 显示名（未知则回退 form 本身）
    function formDisplayName(form) {
        var list = typeEngine.enabledTypes
        for (var i = 0; i < list.length; ++i) {
            if (list[i].form === form)
                return list[i].displayName
        }
        return form || "—"
    }

    // 函数: 取 "YYYY-MM-DDTHH:mm:ssZ" 的日期部分
    // 输入: iso —— 入库时刻；输出: YYYY-MM-DD（空输入→"—"）
    function dateOf(iso) {
        var s = String(iso || "")
        return s.length >= 10 ? s.slice(0, 10) : "—"
    }

    // 函数: 载入目标包当前值并弹出
    // 输入: id —— 包 id；输出: 无
    function openFor(id) {
        var detail = typeEngine.packageDetail(id)
        if (!detail || !detail.id) {
            dialog.failed("无法读取数据包：#" + id)
            return
        }
        dialog.packageId = id
        dialog.typeName = formDisplayName(detail.type_form)
        dialog.createdText = dateOf(detail.created_time)

        titleField.text = detail.title || ""
        // 创作者：详情取完整列表（多作者用、号拼接，与导入侧一致）
        var names = detail.creators || []
        creatorsField.text = names.join("、")
        scoreSpin.value = detail.score > 0 ? detail.score : 0
        creationDateField.text = detail.creation_date || ""
        // 打标签：以该包现有标签初始化树（服务层保证存量集合即反链）
        tagTree.checkedIds = typeEngine.fileTags(id)
        errorText = ""
        open()
        titleField.forceActiveFocus()
    }

    // 函数: 逐项写库（标题 → 创作者 → 评分 → 创作日期 → 标签），任一失败即中止并提示
    // 输入/输出: 无；成功→发 saved 并关闭
    function save() {
        var id = dialog.packageId
        var title = titleField.text.trim()
        if (title === "") {
            errorText = "标题不能为空"
            return
        }
        if (creationDateField.text === "") {
            errorText = "创作日期为必填项，请选择日期"
            return
        }

        var steps = [
            ["标题", typeEngine.updateTitle(id, title)],
            ["创作者", typeEngine.writeCreators(id, creatorsField.text.trim())],
            ["评分", typeEngine.ratePackage(id, scoreSpin.value)],
            ["创作日期", typeEngine.updateCreationDate(id, creationDateField.text)],
            ["标签", typeEngine.writeFileTags(id, tagTree.checkedIds)]
        ]
        for (var i = 0; i < steps.length; ++i) {
            var r = steps[i][1]
            if (!r || !r.ok) {
                errorText = steps[i][0] + "写入失败：" + ((r && r.message) || "未知错误")
                return
            }
        }
        errorText = ""
        dialog.saved(id)
        dialog.close()
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "编辑元数据" + (dialog.packageId > 0 ? "（#" + dialog.packageId + "）" : "")
            font.pixelSize: 15
            font.bold: true
        }

        // 只读背景信息：类型 / 入库时间（不可编辑，仅供确认编辑对象）
        Text {
            Layout.fillWidth: true
            text: "类型：" + dialog.typeName + "　·　入库：" + dialog.createdText
            color: "#7a7a7a"
            font.pixelSize: 11
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            Text { text: "标题"; font.pixelSize: 12 }
            TextField {
                id: titleField
                Layout.fillWidth: true
                Layout.preferredWidth: 300
                placeholderText: "未命名"
                font.pixelSize: 13
            }

            Text { text: "创作者"; font.pixelSize: 12 }
            TextField {
                id: creatorsField
                Layout.fillWidth: true
                Layout.preferredWidth: 300
                placeholderText: "佚名"
                font.pixelSize: 13
            }

            Text { text: "评分"; font.pixelSize: 12 }
            SpinBox {
                id: scoreSpin
                from: 0
                to: 100
                editable: true
            }

            Text { text: "创作日期"; font.pixelSize: 12 }
            DateField {
                id: creationDateField
                Layout.fillWidth: true
                Layout.preferredWidth: 300
                placeholder: "必填（YYYY-MM-DD）"
            }
        }

        // ---- 标签（M6：打标入口并入本弹窗；强制反链）----
        Text {
            Layout.fillWidth: true
            text: "标签"
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            color: "#f7f9fc"
            border.color: "#d5dbe6"
            border.width: 1
            radius: 4

            TagTree {
                id: tagTree
                anchors.fill: parent
                anchors.margins: 4
                enforceAntichain: true
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

        Text {
            Layout.fillWidth: true
            text: "创作者可用「、」或「,」分隔多人，第一个为主要创作者；留空即「佚名」。"
                 + "再次评分按 0.4/0.6 加权（见评分算法）。"
                 + "标签不可与其祖先/后代同时选中：勾选子节点会自动移出已勾选的祖先。"
            color: "#7a7a7a"
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
        }

        Text {
            Layout.fillWidth: true
            visible: dialog.errorText !== ""
            text: dialog.errorText
            color: "#c0392b"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
        }

        // ---- 操作 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
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
                    text: "保存"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.save()
                }
            }
        }
    }
}
