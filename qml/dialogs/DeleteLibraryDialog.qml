// 删库弹窗（开发文档 4.3）：两档删除，实体文件档需二次强确认
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg
    title: "删除库"
    modal: true
    anchors.centerIn: parent ? parent : null
    width: 520
    standardButtons: Dialog.NoButton

    property string libraryName: ""
    property string linkAddress: ""
    // deleteFiles=true：数据集与实体文件一并删除；false：仅删数据集，保留实体
    signal confirmed(bool deleteFiles)

    // 1=方式选择；2=实体文件删除的二次强确认
    property int step: 1

    contentItem: ColumnLayout {
        spacing: 10

        StackLayout {
            Layout.fillWidth: true
            currentIndex: dlg.step - 1

            ColumnLayout {
                spacing: 8
                Label {
                    text: "选择对库「" + dlg.libraryName + "」的删除方式："
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: "· 仅删除数据集：移除软件内的元数据与封面，保留链接库中的实体文件。\n"
                        + "· 删除数据集与实体文件：同时递归删除链接库目录，不可恢复。"
                    wrapMode: Text.WordWrap
                    color: "#555555"
                }
                Label {
                    visible: dlg.linkAddress !== ""
                    text: "链接库目录：" + dlg.linkAddress
                    wrapMode: Text.WordWrap
                    color: "#555555"
                }
            }

            ColumnLayout {
                spacing: 8
                Label {
                    text: "⚠ 最终确认"
                    font.bold: true
                    color: "#c0341d"
                }
                Label {
                    text: "将递归删除「" + dlg.linkAddress + "」下的全部实体文件，\n"
                        + "同时删除库「" + dlg.libraryName + "」的数据集。此操作不可恢复！"
                    wrapMode: Text.WordWrap
                    color: "#c0341d"
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: "取消"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dlg.reject()
        }
        Button {
            visible: dlg.step === 1
            text: "仅删除数据集"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                dlg.confirmed(false)
                dlg.accept()
            }
        }
        Button {
            visible: dlg.step === 1
            text: "删除数据集与实体文件…"
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: dlg.step = 2
        }
        Button {
            visible: dlg.step === 2
            text: "我已知晓，确认删除全部"
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                dlg.confirmed(true)
                dlg.accept()
            }
        }
        Button {
            visible: dlg.step === 2
            text: "返回"
            onClicked: dlg.step = 1
        }
    }

    onOpened: dlg.step = 1
}
