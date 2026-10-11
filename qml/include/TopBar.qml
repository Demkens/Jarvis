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

    property string pendingChannel: ""  // 输入: 导入通道
    property real pendingY: 0           // subY 触发行在 Overlay 坐标系中的 y

    // 函数: 打开/关闭文件面板
    function toggleFilePanel() {
        if (filePanel.opened) {
            closeFileChain()
            return
        }
        pendingChannel = ""     // 重置导入通道
        hoverTimer.stop()       // 关闭延时器
        subTypePopup.close()    // 关闭类型子面板
        const pt = fileArea.mapToItem(Overlay.overlay, 0, fileArea.height)  // 局部坐标转主坐标
        filePanel.x = pt.x
        filePanel.y = pt.y
        filePanel.open()
    }

    // 函数: 关闭文件面板与类型子面板整条链
    function closeFileChain() {
        hoverTimer.stop()       // 关闭延时器
        subTypePopup.close()    // 关闭类型子面板
        filePanel.close()       // 关闭文件面板
    }

    // 函数: 打开（或切换）类型子面板到指定通道，级联在被悬停/点击的行右侧
    function openSubTypePopup(channel, subY) {
        if (subTypePopup.opened && subTypePopup.channel === channel)
            return
        subTypePopup.channel = channel

        subTypePopup.x = filePanel.x + filePanel.width
        subTypePopup.y = subY
        if (!subTypePopup.opened)
            subTypePopup.open()
    }

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
            
            // 文件菜单
            Rectangle {
                id: fileArea
                height: 26
                implicitWidth: fileLabel.implicitWidth + 20
                radius: 4
                color: fileAreaMouse.containsMouse || filePanel.opened ? '#525c75' : "transparent"

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
                    onClicked: toggleFilePanel()
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
    ImportDialog { id: importDialog }

    // 文件面板打开期间的外部点击拦截：点在面板外 → 关闭整条链并吞掉该次点击（模态，
    // 与 Qt 菜单一致；同时避免「第二次点文件按钮：PressOutside 先关面板、onClicked
    // 又把面板重开」的时序 bug）。面板/子面板内部的点击不受影响——Popup 打开后 z 序
    // 高于本层，面板内容会先命中事件。
    MouseArea {
        id: outsideDismisser
        parent: Overlay.overlay
        anchors.fill: parent
        enabled: filePanel.opened || subTypePopup.opened
        onPressed: closeFileChain()
    }

    // 文件面板（父级）：动作列表；悬停/点击导入行后在右侧级联打开类型子面板
    Popup {
        id: filePanel
        parent: Overlay.overlay
        width: 160
        padding: 6
        // 外部点击关闭交给 outsideDismisser；此处保留 Esc 关闭。
        // 若保留 CloseOnPressOutside，点文件按钮的 press 会先关面板，导致按钮
        // onClicked 里 toggleFilePanel 误判为「未打开」而重开面板。
        closePolicy: Popup.CloseOnEscape

        onClosed: {
            hoverTimer.stop()
            subTypePopup.close()
        }

        background: Rectangle {
            radius: 4
            color: '#b7bac0'
            border.width: 1
            border.color: '#a2a9b5'
        }

        contentItem: Column {
            spacing: 2

            FileMenuRow {
                id: wizardRow
                text: "导入数据"
                rowEnabled: importService.isOnline()
                onHovered: {
                    pendingChannel = "wizard"
                    pendingY = wizardRow.mapToItem(Overlay.overlay, 0, 0).y
                    hoverTimer.restart()
                }
                onTriggered: {
                    hoverTimer.stop()
                    openSubTypePopup("wizard", wizardRow.mapToItem(Overlay.overlay, 0, 0).y)
                }
            }
            FileMenuRow {
                id: preprocessRow
                text: "导入预处理数据"
                rowEnabled: importService.isOnline()
                onHovered: {
                    pendingChannel = "preprocess"
                    pendingY = preprocessRow.mapToItem(Overlay.overlay, 0, 0).y
                    hoverTimer.restart()
                }
                onTriggered: {
                    hoverTimer.stop()
                    openSubTypePopup("preprocess", preprocessRow.mapToItem(Overlay.overlay, 0, 0).y)
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#8a92a0" }

            FileMenuRow {
                text: "刷新页面"
                onTriggered: {
                    topBar.refreshRequested()
                    closeFileChain()
                }
            }
        }
    }

    // 类型选择子面板（级联于文件面板右侧，两个导入通道共用，通过 channel 区分）
    SubTypePopup {
        id: subTypePopup
        onTypeSelected: {
            importDialog.start(form, subTypePopup.channel)
            closeFileChain()
        }
    }

    // 延迟定时器：悬停触发
    Timer {
        id: hoverTimer
        interval: 500
        onTriggered: openSubTypePopup(pendingChannel, pendingY)
    }

    // 自绘控件：类型选择子面板
    component SubTypePopup: Popup {
        id: sub

        // 输入：当前通道（"wizard"/"preprocess"），open 前由 openSubTypePopup 设置
        property string channel: ""

        // 输出信号：选定了类型 form
        signal typeSelected(string form)

        parent: Overlay.overlay
        width: 96
        padding: 6
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

        background: Rectangle {
            radius: 4
            color: '#b7bac0'
            border.width: 1
            border.color: '#a2a9b5'
        }

        contentItem: Column {
            spacing: 2

            Repeater {
                model: typeEngine.enabledTypes
                delegate: Rectangle {
                    required property var modelData
                    width: sub.width - 12   // 扣除 padding 6*2
                    height: 30
                    radius: 3
                    color: typeItemMouse.containsMouse ? "#9aa3b3" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: modelData.displayName
                        color: "#1e2430"
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: typeItemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: sub.typeSelected(modelData.form)
                    }
                }
            }
        }
    }

    // 自绘控件：文件面板中的一行动作
    component FileMenuRow: Rectangle {
        id: row

        // 输入：行标题、行是否可用（离线时置灰导入行）
        property string text: ""
        property bool rowEnabled: true

        // 输出信号：鼠标悬停（可用时）、鼠标点击
        signal hovered()
        signal triggered()

        width: parent.width
        height: 30
        radius: 3
        color: !row.rowEnabled ? "transparent" : (rowMouse.containsMouse ? "#9aa3b3" : "transparent")

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            text: row.text
            color: row.rowEnabled ? "#1e2430" : "#8a92a0"
            font.pixelSize: 13
        }

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            onEntered: if (row.rowEnabled) row.hovered()
            onClicked: if (row.rowEnabled) row.triggered()
        }
    }

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