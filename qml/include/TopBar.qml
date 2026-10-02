// 顶边功能栏：库管理区（可点击 → 呼出库管理窗口）+ 设置入口 + 窗口控制按钮组。
// 状态栏/提示链路已移除，操作结果仅控制台日志。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import "../dialogs"

Item {
    id: topBar

    signal libraryAreaClicked()     // 信号: 打开库管理窗口
    signal refreshRequested()       // 信号: 刷新资源列表

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
            
            // 文件菜单：导入数据包 / 刷新
            Rectangle {
                id: fileArea
                height: 26
                implicitWidth: fileLabel.implicitWidth + 20
                radius: 4
                color: fileAreaMouse.containsMouse || fileMenu.visible ? '#525c75' : "transparent"

                Layout.alignment: Qt.AlignVCenter

                Text {
                    id: fileLabel
                    anchors.centerIn: parent
                    text: "文件"
                    font.pixelSize: 13
                    color: "#e6eaf2"
                }

                MouseArea {
                    id: fileAreaMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: fileMenu.popup(fileArea, 0, fileArea.height)
                }

                Menu {
                    id: fileMenu
                    MenuItem {
                        text: "导入数据"
                        onTriggered: importDialog.open()
                    }
                    MenuItem {
                        text: "刷新页面"
                        onTriggered: topBar.refreshRequested()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // 设置入口
            WindowButton {
                iconSource: "qrc:/img/Resources/title/setting.png"
                onClicked: settingsDialog.open()
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
            WindowButton {
                iconSource: "qrc:/img/Resources/title/min.png"
                onClicked: topBar.Window.window.showMinimized()
            }
            WindowButton {
                iconSource: topBar.Window.window.visibility === Window.Maximized ? "qrc:/img/Resources/title/mini.png" : "qrc:/img/Resources/title/max.png"
                onClicked: {
                    const w = topBar.Window.window
                    if (w.visibility === Window.Maximized) w.showNormal()
                    else w.showMaximized()
                }
            }
            WindowButton {
                iconSource: "qrc:/img/Resources/title/close.png"
                onClicked: Qt.quit()
            }
        }
    }

    // 设置弹窗实例
    SettingsDialog {
        id: settingsDialog
        onAccepted: topBar.refreshRequested()
    }

    // 导入弹窗实例
    ImportDialog {id: importDialog}

    // 自绘控件：窗口控制按钮
    component WindowButton: Image {
        id: winBtn

        // 输入：图标资源路径（qrc 绝对路径）
        property string iconSource: ""

        // 输出信号：鼠标点击后触发，由调用方绑定具体窗口行为
        signal clicked()

        Layout.preferredWidth: 30; Layout.preferredHeight: 30
        sourceSize.width: 128; sourceSize.height: 128
        fillMode: Image.PreserveAspectFit
        source: winBtn.iconSource

        Layout.alignment: Qt.AlignVCenter

        // 悬停提亮
        layer.enabled: false
        layer.effect: MultiEffect {
            colorizationColor: "white"
            colorization: 1.0
            brightness: 0.5
        }

        MouseArea {
            hoverEnabled: true

            anchors.fill: parent

            onEntered: winBtn.layer.enabled = true
            onExited: winBtn.layer.enabled = false
            onClicked: winBtn.clicked()
        }
    }
}