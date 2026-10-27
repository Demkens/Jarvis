// 顶层对话框/窗口实例的集中编排（导入/设置/查看器/详情/包导出）。
// 建库/删库/库切换已上移至库管理窗口（LibraryManagerWindow），不归此处管理；
// Main.qml 只负责"转发哪个入口触发哪扇窗"，本组件负责实例与各回调结果的处理。
// 状态栏/提示链路已移除，操作结果仅控制台日志。
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import "../windows"
import "../dialogs"

Item {
    id: appDialogs

    // 导出目标包 id（内部维护，Main 不感知细节）
    property int _exportTargetId: 0

    // 设置弹窗确认后抛出（Main 转发给资源列表刷新）
    signal settingsAccepted()

    // ---- 对外可调用方法：Main 的信号处理器转入此处 ----
    function openViewer(id) { viewerWindow.openPackage(id) }
    // 详情窗口：与 ViewerWindow 同级顶层实例化（Window 挂在深层 Item 下会弹不出）。
    // 其「编辑」按钮使用详情窗口自带 EditPackageDialog 实例（弹窗须渲染在所属窗口的 overlay 内）
    function openDetail(id) { detailWindow.openPackage(id) }
    // 元数据编辑弹窗（资源页右键、详情窗口「编辑」按钮两处入口）
    function openEditPackage(id) { editDialog.openFor(id) }
    function openExport(id) {
        _exportTargetId = id
        // 默认定位到设置里的默认导出位置（未设置则不干预，用系统默认目录）
        const dirUrl = typeEngine.exportDirUrl()
        if (dirUrl !== "")
            exportFolderPicker.currentFolder = dirUrl
        exportFolderPicker.open()
    }
    // 设置弹窗（顶栏设置按钮 → Main → 此处）
    function openSettings() { settingsDialog.open() }
    // 标签管理弹窗（顶栏「工具 → 标签管理…」→ Main → 此处）
    function openTagManager() { tagManagerDialog.openManager() }
    // 创作者管理弹窗（顶栏「工具 → 创作者管理…」→ Main → 此处；M11a）
    function openCreatorManager() { creatorManagerDialog.openManager() }
    // 图片查重窗口（顶栏「工具 → 图片查重…」→ Main → 此处）：单实例，显示并刷新指纹状态
    function openDedup() { dedupWindow.openWindow() }
    // 标签导出（顶栏「工具 → 标签导出…」→ Main → 此处）：直接写入默认导出位置，无弹窗
    function exportTags() {
        const result = typeEngine.exportTags()
        if (result && result.ok)
            console.log("[tag] 已导出标签到: " + result.path)
        else
            console.warn("[tag] 标签导出失败: " + ((result && result.message) || "未知错误"))
    }
    // 标签导入（顶栏「工具 → 标签导入…」→ Main → 此处）：选文件后合并进现有标签树
    function importTags() {
        const dirUrl = typeEngine.exportDirUrl()
        if (dirUrl !== "")
            tagFilePicker.currentFolder = dirUrl
        tagFilePicker.open()
    }
    // 导入弹窗直达指定步骤（文件菜单选定类型后 → Main → 此处）
    function startImport(form, channel) { importDialog.start(form, channel) }
    // 设置弹窗的「默认导出位置」选择：打开选择器（默认定位到当前设置值），选定后回填对话框
    function pickSettingsExportDir() {
        const dirUrl = typeEngine.exportDirUrl()
        if (dirUrl !== "")
            settingsExportPicker.currentFolder = dirUrl
        settingsExportPicker.open()
    }

    // ---- 实例定义与回调 ----
    ImportDialog {
        id: importDialog
    }

    SettingsDialog {
        id: settingsDialog
        onAccepted: appDialogs.settingsAccepted()
        // 设置里的「默认导出位置」选择：由本组件顶层选择器完成（Popup 内不嵌套原生选择器）
        onExportDirPickRequested: appDialogs.pickSettingsExportDir()
    }

    ViewerWindow {
        id: viewerWindow
        onOpenFailed: function (message) { console.warn("[viewer] " + message) }
    }

    DetailWindow {
        id: detailWindow
    }

    // 资源页「编辑」入口的弹窗（详情窗口的编辑入口使用其自带实例，见 DetailWindow.qml）
    EditPackageDialog {
        id: editDialog
        // 保存成功后：详情窗口若正展示该包则重读；资源列表由 PackageUpdated → TypeEngine.packagesChanged 自动刷新
        onSaved: function (id) { detailWindow.refreshIfCurrent(id) }
        onFailed: function (message) { console.warn("[edit] " + message) }
    }

    // 标签管理（全库共享标签树的维护入口）
    TagManagerDialog {
        id: tagManagerDialog
    }

    // 创作者管理（M11a：正式名重命名、别名增删）经工具菜单唤起
    CreatorManagerDialog {
        id: creatorManagerDialog
    }

    // 图片查重窗口：独立顶层窗口；库内结果行可跳转详情窗口
    DedupWindow {
        id: dedupWindow
        onDetailRequested: function (fileId) { detailWindow.openPackage(fileId) }
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

    // 标签导入：选择标签文件（*.json）后合并进现有标签树
    FileDialog {
        id: tagFilePicker
        title: "选择标签文件"
        fileMode: FileDialog.OpenFile
        nameFilters: ["标签文件 (*.json)"]
        onAccepted: {
            var local = String(selectedFile)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            local = decodeURIComponent(local) // %20 等还原为真实路径
            const result = typeEngine.importTags(local)
            if (result && result.ok)
                console.log("[tag] 标签导入完成：新增 " + result.created + "，复用 " + result.merged)
            else
                console.warn("[tag] 标签导入失败: " + ((result && result.message) || "未知错误"))
        }
    }

    // 设置弹窗的「默认导出位置」选择器（选定后回填设置对话框，不在此落盘）
    FolderDialog {
        id: settingsExportPicker
        title: "选择默认导出位置"
        onAccepted: {
            var local = String(selectedFolder)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            settingsDialog.setExportDirPicked(decodeURIComponent(local))
        }
    }
}
