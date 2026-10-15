// 标签管理弹窗（M6）：全库共享标签树的维护入口（顶栏「工具 → 标签管理…」唤起）。
// 职责：新建顶级标签、为选中标签新建子标签、重命名、删除（级联删除全部子孙与关联行）。
//      所有写操作都经 TypeEngine 门面；成功后门面会刷新 tagTree，本弹窗的树随之更新。
// 数据源：typeEngine.tagTree（扁平 DFS 序）。
// 对外方法：openManager() —— 复位状态后弹出。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../include"

Popup {
    id: dialog

    // 树中当前选中的标签 id（0=未选）
    property int currentId: 0
    // 提示文本（空=无提示）
    property string errorText: ""
    // 是否处于删除确认态（避免嵌套弹窗，改为就地确认）
    property bool confirmDelete: false

    width: 440
    height: 540
    modal: true
    focus: true
    padding: 16
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    // 函数: 在标签树里按 id 找名称
    // 输入: id —— 标签 id；输出: 名称（找不到返回空串）
    function tagNameOf(id) {
        var list = typeEngine.tagTree
        for (var i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return list[i].name
        }
        return ""
    }

    // 函数: 复位状态并弹出
    // 输入/输出: 无
    function openManager() {
        dialog.currentId = 0
        tagTree.currentId = 0
        dialog.confirmDelete = false
        dialog.errorText = ""
        nameField.text = ""
        open()
    }

    // 函数: 统一的写操作包装（转发门面结果到 errorText；成功时清空输入框）
    // 输入: result —— 门面返回的 {ok, message?}；输出: 成功返回 true
    function applyResult(result) {
        if (result && result.ok) {
            dialog.errorText = ""
            nameField.text = ""
            return true
        }
        dialog.errorText = (result && result.message) ? result.message : "操作失败"
        return false
    }

    // 函数: 新建顶级标签
    // 输入/输出: 无
    function createTopLevel() {
        var name = nameField.text.trim()
        if (name === "") {
            errorText = "请输入标签名"
            return
        }
        applyResult(typeEngine.createTag(0, name))
    }

    // 函数: 为当前选中标签新建子标签
    // 输入/输出: 无
    function createChild() {
        if (dialog.currentId <= 0) {
            errorText = "请先在树中选择父标签"
            return
        }
        var name = nameField.text.trim()
        if (name === "") {
            errorText = "请输入标签名"
            return
        }
        applyResult(typeEngine.createTag(dialog.currentId, name))
    }

    // 函数: 重命名当前选中标签（不改变层级）
    // 输入/输出: 无
    function renameCurrent() {
        if (dialog.currentId <= 0) {
            errorText = "请先在树中选择要重命名的标签"
            return
        }
        var name = nameField.text.trim()
        if (name === "") {
            errorText = "请输入新名称"
            return
        }
        applyResult(typeEngine.renameTag(dialog.currentId, name))
    }

    // 函数: 确认删除当前选中标签（级联删除子孙与关联行）
    // 输入/输出: 无
    function confirmDeleteCurrent() {
        var result = typeEngine.deleteTag(dialog.currentId)
        dialog.confirmDelete = false
        if (result && result.ok) {
            dialog.currentId = 0
            tagTree.currentId = 0
            dialog.errorText = ""
        } else {
            dialog.errorText = (result && result.message) ? result.message : "删除失败"
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "标签管理"
            font.pixelSize: 15
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "标签全库共享、与类型无关；点击行选中，可对选中标签新建子标签 / 重命名 / 删除。"
            color: "#7a7a7a"
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
        }

        // 标签树容器（带边框与浅底）
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            color: "#f7f9fc"
            border.color: "#d5dbe6"
            border.width: 1
            radius: 4

            TagTree {
                id: tagTree
                anchors.fill: parent
                anchors.margins: 4
                selectable: false
                tagModel: typeEngine.tagTree
                // 注意：不写 currentId 的声明式绑定——TagTree 内部选中时会赋值自身 currentId，
                // 会打断外部绑定；改为「内部选中 → 回传 dialog，dialog 复位时显式写回树」
                onCurrentChanged: function (id) {
                    dialog.currentId = id
                }
            }

            Text {
                anchors.centerIn: parent
                visible: (typeEngine.tagTree || []).length === 0
                text: "暂无标签，可在下方输入名称后新建"
                color: "#9aa3b2"
                font.pixelSize: 11
            }
        }

        Text {
            Layout.fillWidth: true
            text: "当前选中：" + (dialog.currentId > 0 ? dialog.tagNameOf(dialog.currentId) : "（无）")
            color: "#7a7a7a"
            font.pixelSize: 11
        }

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "标签名称（新建用 / 重命名用）"
            font.pixelSize: 13
            selectByMouse: true
        }

        // 常规操作行 / 删除确认行（互斥显示，避免嵌套弹窗）
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !dialog.confirmDelete

            Rectangle {
                Layout.preferredWidth: 92
                Layout.preferredHeight: 30
                radius: 4
                color: "#e6eaf2"
                Text {
                    anchors.centerIn: parent
                    text: "新建顶级"
                    color: "#2b2b2b"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.createTopLevel()
                }
            }
            Rectangle {
                Layout.preferredWidth: 92
                Layout.preferredHeight: 30
                radius: 4
                color: "#e6eaf2"
                Text {
                    anchors.centerIn: parent
                    text: "新建子标签"
                    color: "#2b2b2b"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.createChild()
                }
            }
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 30
                radius: 4
                color: "#e6eaf2"
                Text {
                    anchors.centerIn: parent
                    text: "重命名"
                    color: "#2b2b2b"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.renameCurrent()
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 30
                radius: 4
                color: "#c0392b"
                Text {
                    anchors.centerIn: parent
                    text: "删除"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (dialog.currentId <= 0) {
                            dialog.errorText = "请先在树中选择要删除的标签"
                            return
                        }
                        dialog.errorText = ""
                        dialog.confirmDelete = true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: dialog.confirmDelete

            Text {
                Layout.fillWidth: true
                text: "将删除标签「" + dialog.tagNameOf(dialog.currentId) + "」及其全部子标签，且不可恢复。"
                color: "#c0392b"
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }
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
                    onClicked: dialog.confirmDelete = false
                }
            }
            Rectangle {
                Layout.preferredWidth: 76
                Layout.preferredHeight: 30
                radius: 4
                color: "#c0392b"
                Text {
                    anchors.centerIn: parent
                    text: "确认删除"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.confirmDeleteCurrent()
                }
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

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 30
                radius: 4
                color: "#86b2ff"
                Text {
                    anchors.centerIn: parent
                    text: "关闭"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.close()
                }
            }
        }
    }
}
