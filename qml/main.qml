// 多进程入口编排：资源主窗口（ApplicationWindow） + 库管理窗口（LibraryManagerWindow）。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "dialogs"
import "include"
import "pages"
import "windows"

// 多进程入口
Item {
    id: root

    // 有库 → 进主窗口；无库 → 库管理门禁。
    Component.onCompleted: {
        if (libraryService.currentName !== ""){
            mainWinLoader.active = true
            mainWinLoader.item.show()
        } else {
            managerLoader.active = true
            managerLoader.item.show()     
        }
    }

    // 软件主窗口：随用随建，关闭即毁
    Loader {
        id: mainWinLoader
        active: false
        sourceComponent: ApplicationWindow {
            visible: false
            width: 1360; height: 865
            minimumWidth: 1300; minimumHeight: 800
            color: "#181d27"
            flags: Qt.FramelessWindowHint | Qt.Window | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowMinimizeButtonHint

            onClosing: mainWinLoader.active = false

            header: TopBar {
                id: topRoot
                height: 35
                
                onLibraryAreaClicked: {
                    managerLoader.active = true
                    managerLoader.item.show()
                }
                onRefreshRequested: resourcePageView.reload()
            }

            // 左栏 + 主区
            RowLayout {
                anchors.fill: parent
                spacing: 0

                LeftBar { }

                ResourcePage {
                    id: resourcePageView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onOpenViewer: function (id) { dialogs.openViewer(id) }
                    onOpenDetail: function (id) { dialogs.openDetail(id) }
                    onExportPackage: function (id) { dialogs.openExport(id) }
                }
            }

            // 顶层对话框/窗口集中编排（导入/查看器/详情/包导出）
            AppDialogs {id: dialogs}
        }
    }

    // 库管理窗口：随用随建，关闭即毁
    Loader {
        id: managerLoader
        active: false
        sourceComponent: LibraryManagerWindow {
            onClosing: managerLoader.active = false
        }
    }
}