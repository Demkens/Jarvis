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

    readonly property bool libraryOpen: libraryService.currentName !== ""

    Component.onCompleted: {
        if (root.libraryOpen)
            mainWin.show()
        else
            libraryManager.show()
    }

    // 软件主窗口
    ApplicationWindow {
        id: mainWin
        visible: false
        width: 1360
        height: 865
        minimumWidth: 1300
        minimumHeight: 800
        color: "#181d27"
        flags: Qt.FramelessWindowHint | Qt.Window | Qt.WindowSystemMenuHint
               | Qt.WindowMaximizeButtonHint | Qt.WindowMinimizeButtonHint

        header: TopBar {
            id: topRoot
            height: 35
            
            onLibraryAreaClicked: libraryManager.show()
            onSettingsSaved: resourcePageView.reload()
        }

        // 左栏 + 主区
        RowLayout {
            anchors.fill: parent
            spacing: 0

            LeftBar {}

            ResourcePage {
                id: resourcePageView
                Layout.fillWidth: true
                Layout.fillHeight: true
                onOpenViewer: function (id) { dialogs.openViewer(id) }
                onOpenDetail: function (id) { dialogs.openDetail(id) }
                onRequestImport: dialogs.openImport()
                onExportPackage: function (id) { dialogs.openExport(id) }
            }
        }

        // 顶层对话框/窗口集中编排（导入/查看器/详情/包导出）
        AppDialogs {
            id: dialogs
        }
    }

    // 库管理窗口
    LibraryManagerWindow {
        id: libraryManager
        visible: false
    }
}