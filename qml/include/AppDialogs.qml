// 顶层对话框/窗口实例的集中编排（导入/查看器/详情/包导出）。
// 建库/删库/库切换已上移至库管理窗口（LibraryManagerWindow），不归此处管理；
// 设置弹窗已下放至 TopBar 就地实例化。
// Main.qml 只负责"转发哪个入口触发哪扇窗"，本组件负责实例与各回调结果的处理。
// 状态栏/提示链路已移除，操作结果仅控制台日志。
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import "../dialogs"
import "../windows"

Item {
    id: appDialogs

    // 导出目标包 id（内部维护，Main 不感知细节）
    property int _exportTargetId: 0

    // ---- 对外可调用方法：Main 的信号处理器转入此处 ----
    function openImport() { importDialog.open() }
    function openViewer(id) { viewerWindow.openPackage(id) }
    // 详情窗口：与 ViewerWindow 同级顶层实例化（Window 挂在深层 Item 下会弹不出）
    function openDetail(id) { detailWindow.openPackage(id) }

    function openExport(id) {
        _exportTargetId = id
        exportFolderPicker.open()
    }

    // ---- 实例定义与回调 ----
    ImportDialog {
        id: importDialog
    }

    ViewerWindow {
        id: viewerWindow
        onOpenFailed: function (message) { console.warn("[viewer] " + message) }
    }

    DetailWindow {
        id: detailWindow
    }

    // 导出目标：选择空文件夹后原样复原包文件（开发文档 5.6）
    FolderDialog {
        id: exportFolderPicker
        title: "导出到空文件夹"
        onAccepted: {
            var local = String(selectedFolder)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            local = decodeURIComponent(local) // %20 等还原为真实路径
            const result = importService.exportPackage(appDialogs._exportTargetId, local)
            if (result.ok)
                console.log("[storage] 已导出 " + result.count + " 个文件到: " + local)
            else
                console.warn("[storage] 导出失败: " + result.message)
        }
    }
}