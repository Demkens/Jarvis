// 顶边功能栏：纯容器 + 功能区组装 + 信号转发。
// 库管理区、设置按钮保留就地；文件菜单（FileMenu）与窗口控制（WindowControls）
import QtQuick
import QtQuick.Layouts

import "."

Item {
    id: topBar

    // 信号: 打开库管理窗口
    signal libraryAreaClicked()
    // 信号: 刷新资源列表（设置确认 / 文件菜单"刷新页面"触发）
    signal refreshRequested()
    // 信号: 选定了导入类型
    signal fileImportRequested(string form, string channel)
    // 信号: 打开设置弹窗
    signal settingsRequested()

    // 顶栏衬底
    Rectangle {
        anchors.fill: parent
        color: "#272f3d"

        // 顶栏空白区拖动，调用系统级窗口移动函数
        MouseArea {
            anchors.fill: parent
            onPressed: topBar.Window.window.startSystemMove()
        }

        // 水平管理布局
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 0

            // 库管理区：● 为在线状态（绿/红），点击呼出库管理窗口
            Rectangle {
                id: libArea
                Layout.alignment: Qt.AlignVCenter
                height: 26
                radius: 4
                color: libAreaMouse.containsMouse ? '#525c75' : "transparent"
                implicitWidth: libRow.implicitWidth + 20

                // 库管理区文本部分
                Row {
                    id: libRow
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6

                    Text {
                        text: "Jarvis"
                        font.pixelSize: 13
                        font.bold: true
                        color: "#e6eaf2"
                    }

                    Text {
                        text: " ● "
                        font.pixelSize: 13
                        color: importService.isOnline() ? "#4caf7d" : "#e5675f"
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: libraryService.currentName
                        font.pixelSize: 13
                        font.bold: true
                        color: "#e6eaf2"
                    }
                }

                // 库管理区点击相应
                MouseArea {
                    id: libAreaMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: topBar.libraryAreaClicked()
                }
            }

            // 分隔线
            Rectangle {
                width: 2; height: 22
                color: "#455164"

                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 5
                Layout.rightMargin: 5
            }

            // 文件功能菜单
            FileMenu {
                Layout.alignment: Qt.AlignVCenter
                onFileImportRequested: function (form, channel) { topBar.fileImportRequested(form, channel) }
                onRefreshRequested: topBar.refreshRequested()
            }

            Item { Layout.fillWidth: true }

            // 设置入口
            WindowButton {
                iconSource: "qrc:/img/Resources/title/setting.png"
                onClicked: topBar.settingsRequested()
            }

            // 分隔线
            Rectangle {
                width: 2; height: 22
                color: "#455164"

                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 5
                Layout.rightMargin: 5
            }

            // 窗口控制按钮组：最小化 / 最大化 / 关闭
            WindowControls { }
        }
    }
}