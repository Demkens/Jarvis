// 全局设置对话框（M5）：每页数量 / 封面长边 / 评分权重。
// 打开时从核心读当前值（typeEngine.settings），保存时写回
// （typeEngine.saveSettings → envs/settings.json 原子写 + 立即应用到各服务）。
// 对话框不感知"设置如何生效"，只负责取值/提交；成功与否经 onAccepted 由调用方提示。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    title: "设置"
    modal: true
    anchors.centerIn: parent ? parent : null
    width: 440
    height: 300
    standardButtons: Dialog.NoButton
    padding: 0

    // 保存失败时的错误文案（状态栏由 Main.qml 的 onAccepted 负责提示成功）
    property string errText: ""

    background: Rectangle {
        radius: 6
        color: "#f0f0f0"
        border.width: 1
        border.color: "#b8b8b8"
    }

    // 打开时重读当前设置：上次未保存的修改不残留
    onOpened: {
        const s = typeEngine.settings()
        pageSpin.value = s.pageSize
        coverSpin.value = s.coverLongEdge
        weightSpin.value = s.scoreRecentWeight
        errText = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: "每页数量"
                Layout.fillWidth: true
                font.pixelSize: 14
            }
            SpinBox {
                id: pageSpin
                from: 10; to: 200; stepSize: 10
                editable: true
                Layout.preferredWidth: 130
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: "封面长边 (px)"
                Layout.fillWidth: true
                font.pixelSize: 14
            }
            SpinBox {
                id: coverSpin
                from: 64; to: 2048; stepSize: 32
                editable: true
                Layout.preferredWidth: 130
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: "评分权重 (%)"
                Layout.fillWidth: true
                font.pixelSize: 14
            }
            SpinBox {
                id: weightSpin
                from: 0; to: 100; stepSize: 5
                editable: true
                Layout.preferredWidth: 130
            }
        }

        Text {
            text: "评分权重 = 二次评分时新评分占比（旧分 ×(1-w) + 新分 ×w），0 = 始终用旧分"
            color: "#888888"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text {
            visible: dialog.errText !== ""
            text: dialog.errText
            color: "#c0341d"
            font.pixelSize: 12
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            Button {
                text: "取消"
                onClicked: dialog.reject()
            }
            Button {
                text: "保存"
                highlighted: true
                onClicked: {
                    // editable SpinBox 编辑中先失焦提交文本，再读取 value（否则可能读到旧值）
                    pageSpin.focus = false
                    coverSpin.focus = false
                    weightSpin.focus = false

                    const result = typeEngine.saveSettings({
                        pageSize: pageSpin.value,
                        coverLongEdge: coverSpin.value,
                        scoreRecentWeight: weightSpin.value
                    })
                    if (!result.ok) {
                        dialog.errText = result.message || "保存失败"
                        return
                    }
                    dialog.accept()
                }
            }
        }
    }
}
