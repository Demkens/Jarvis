// 多进程入口：资源主窗口(ApplicationWindow) + 库管理窗口(LibraryManagerWindow)
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

    // 有库 → 进主窗口；无库 → 库管理门禁
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
            // 无窗口标签
            flags: Qt.FramelessWindowHint | Qt.Window | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowMinimizeButtonHint

            onClosing: mainWinLoader.active = false

            // 当前页面索引
            property int pageIndex: 0

            // 顶边功能栏
            header: TopBar {
                height: 35
                
                // 槽函数: 打开库管理窗口
                onLibraryAreaClicked: { managerLoader.active = true; managerLoader.item.show() }
                // 槽函数: 刷新资源列表
                onRefreshRequested: resourcePageView.reload()
                // 槽函数: 选定了导入类型
                onFileImportRequested: function (form, channel) { dialogs.startImport(form, channel) }
                // 槽函数: 打开设置弹窗
                onSettingsRequested: dialogs.openSettings()
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // 左侧导航栏
                LeftBar {
                    currentIndex: pageIndex
                    onPageSelected: function (i) { pageIndex = i }
                }

                // 主内容区
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: pageIndex

                    ResourcePage {
                        id: resourcePageView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onOpenViewer: function (id) { dialogs.openViewer(id) }
                        onOpenDetail: function (id) { dialogs.openDetail(id) }
                        onExportPackage: function (id) { dialogs.openExport(id) }
                    }
                    StatisticsPage {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    NotePage {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }

            // 弹窗集中编排(导入/导出/设置/查看器等)
            AppDialogs {
                id: dialogs
                onSettingsAccepted: resourcePageView.reload()
            }
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