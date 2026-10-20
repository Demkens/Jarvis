// 标签选择弹窗（M7 统计页「标签计量」维度用）。
// 职责: 只做一件事——从全库标签树里挑若干标签，并把选中集合回传调用方。
//       标签树复用 TagTree，作自由多选（enforceAntichain=false，与资源页筛选弹窗一致）；
//       本弹窗不持有统计口径，选中的标签由统计页保存并交给 statsService.tagStats()。
// 数据源: typeEngine.tagTree（全库共享标签树，库切换/结构变更后自动刷新）。
// 对外方法: openWith(tagIds) —— 以已选标签初始化后弹出；
// 对外信号: applied(var tagIds) —— 点「确定」时发出（标签 id 数组，按标签树先序）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../include"

Popup {
    id: dialog

    // 输出: 确认选择（tagIds 为标签 id 数组，空数组表示清空选择）
    signal applied(var tagIds)

    width: 380
    height: 520
    modal: true
    focus: true
    padding: 16
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    // 函数: 以当前已选标签初始化后弹出
    // 输入: tagIds —— 已选标签 id 数组（可为空）；输出: 无
    function openWith(tagIds) {
        // 拷贝一份，避免与页面持有的数组共享引用
        tagTree.checkedIds = (tagIds || []).slice(0)
        open()
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "选择标签"
            font.pixelSize: 15
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "选中标签即统计「打了该标签或其全部子孙」的数据包数量。"
            font.pixelSize: 11
            color: "#7a7a7a"
            wrapMode: Text.Wrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f7f9fc"
            border.color: "#d5dbe6"
            border.width: 1
            radius: 4

            TagTree {
                id: tagTree
                anchors.fill: parent
                anchors.margins: 4
                tagModel: typeEngine.tagTree
                enforceAntichain: false
            }

            Text {
                anchors.centerIn: parent
                visible: (typeEngine.tagTree || []).length === 0
                text: "暂无标签（可在顶栏「工具 → 标签管理…」中创建）"
                color: "#9aa3b2"
                font.pixelSize: 11
            }
        }

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
                    text: "清空"
                    color: "#2b2b2b"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: tagTree.checkedIds = []
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
                    text: "确定"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        dialog.applied(tagTree.checkedIds.slice(0))
                        dialog.close()
                    }
                }
            }
        }
    }
}