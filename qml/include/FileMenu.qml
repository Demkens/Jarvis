// 文件菜单子系统：触发按钮 + 文件面板 + 类型子面板（级联）+ 外部点击拦截 + 悬停定时器。
import QtQuick
import QtQuick.Controls

import "."

Item {
    id: fileMenu
    // 隐式尺寸交由触发按钮决定（外层 Layout 按此摆放）
    implicitWidth: fileArea.implicitWidth; implicitHeight: fileArea.height

    // 悬停延时的待定通道与触发行在 Overlay 坐标系中的 y
    property string pendingChannel: ""
    property real pendingY: 0

    // 信号: 选定了导入类型（form 与通道由调用方决定怎么用）
    signal fileImportRequested(string form, string channel)
    // 信号: 菜单内"刷新页面"被触发
    signal refreshRequested()

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

    // 文件菜单触发按钮
    Rectangle {
        id: fileArea
        implicitWidth: fileLabel.implicitWidth + 20; height: 26
        radius: 4
        color: fileAreaMouse.containsMouse || filePanel.opened ? '#525c75' : "transparent"

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

    // 文件面板打开期间的外部点击拦截：点在面板外 → 关闭整条链并吞掉该次点击
    // 高于本层，面板内容会先命中事件
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
                    fileMenu.refreshRequested()
                    closeFileChain()
                }
            }
        }
    }

    // 类型选择子面板（级联于文件面板右侧，两个导入通道共用，通过 channel 区分）
    SubTypePopup {
        id: subTypePopup
        onTypeSelected: {
            fileMenu.fileImportRequested(form, channel)
            closeFileChain()
        }
    }

    // 延迟定时器：悬停触发
    Timer {
        id: hoverTimer
        interval: 500
        onTriggered: openSubTypePopup(pendingChannel, pendingY)
    }
}