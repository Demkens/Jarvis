// 主窗口五区框架（开发文档 8.1）：顶栏（库名/导入/新建/设置占位）+ 左栏（四键导航）
// + 主区（资源显示页 / 库管理页）+ 状态栏（计数/库名/在线状态）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import "dialogs"
import "windows"

ApplicationWindow {
    id: root
    width: 1360; height: 865
    minimumWidth: 1300; minimumHeight: 800
    visible: true
    title: "Jarvis" + (libraryService.currentName ? " - " + libraryService.currentName : "")
    color: "#eef1f6"

    // true=资源页，false=库管理页；无打开库时强制管理页
    property bool resourcePage: false
    readonly property bool libraryOpen: libraryService.currentName !== ""

    // 函数: 规格化字节数（库管理页沿用）
    function humanSize(bytes) {
        const KB = 1024
        const MB = KB * 1024
        const GB = MB * 1024

        if (bytes >= GB)
            return (bytes / GB).toFixed(2) + " GB"
        if (bytes >= MB)
            return (bytes / MB).toFixed(1) + " MB"
        if (bytes >= KB)
            return (bytes / KB).toFixed(1) + " KB"
        return bytes + " B"
    }

    // ============ 顶栏 ============
    header: Rectangle {
        height: 52
        color: "#d8d8d8"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            Text {
                text: "Jarvis  ·  当前库：" + (libraryService.currentName || "（未打开）")
                font.pixelSize: 15
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // 新建库入口常驻顶栏（用户选定方案 2）：无库时可建库，有库时也可继续新建第二个库；
            // 资源页的"导入数据包"按钮移入 ResourcePage 内（requestImport 信号）
            Button {
                text: "新建库"
                highlighted: true
                onClicked: createDialog.open()
            }
            Button {
                text: "设置"
                onClicked: settingsDialog.open()
            }
        }
    }

    // ============ 左栏 + 主区 ============
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 左栏导航（开发文档 8.1 四键；批量/统计/笔记一期占位）----
        Rectangle {
            Layout.preferredWidth: 128
            Layout.fillHeight: true
            color: "#e4e8f0"

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 10
                spacing: 2

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: root.resourcePage ? "#3d6ecd" : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "资源显示"
                        color: root.resourcePage ? "#ffffff" : "#2b2b2b"
                        font.pixelSize: 14
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.resourcePage = true
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "批量操作"
                        color: "#9aa3b2"
                        font.pixelSize: 14
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.showStatus("批量操作二期实装", false)
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "数据统计"
                        color: "#9aa3b2"
                        font.pixelSize: 14
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.showStatus("数据统计二期实装", false)
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "短笔记"
                        color: "#9aa3b2"
                        font.pixelSize: 14
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.showStatus("短笔记二期实装", false)
                    }
                }
            }
        }

        // ---- 主区 ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 资源显示页（M4）
            ResourcePage {
                id: resourcePageView
                visible: root.resourcePage && root.libraryOpen
                anchors.fill: parent
                onOpenViewer: function (id) { viewerWindow.openPackage(id) }
                onRequestImport: importDialog.open()
                onExportPackage: function (id) {
                    root.exportTargetId = id
                    exportFolderPicker.open()
                }
            }

            // 库管理页（M1 仪表盘，保留）
            ColumnLayout {
                visible: !root.resourcePage || !root.libraryOpen
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Text {
                    visible: libraryService.libraries.length === 0
                    text: "尚未创建任何库。点击右上角「新建库」开始：\n"
                        + "数据集（元数据、封面）存于软件内 envs 目录，实体文件存于你指定的链接库目录。"
                    color: "#666666"
                    font.pixelSize: 14
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: libraryService.libraries

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 86
                        radius: 4
                        color: modelData.isCurrent ? "#cfe2f3" : "#f5f5f5"
                        border.width: 1
                        border.color: "#cfcfcf"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    text: (modelData.isCurrent ? "● " : "○ ") + modelData.name
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                                Text {
                                    text: "链接库：" + modelData.linkAddress
                                          + "    类型：" + (modelData.support.length
                                              ? modelData.support.join(", ") : "（未启用）")
                                    color: "#555555"
                                    font.pixelSize: 12
                                }
                            }

                            Button {
                                text: "切换"
                                enabled: !modelData.isCurrent
                                onClicked: {
                                    const result = libraryService.switchLibrary(modelData.name)
                                    if (!result.ok)
                                        root.showStatus(result.message, true)
                                    else
                                        root.resourcePage = true
                                }
                            }
                            Button {
                                text: "删除"
                                onClicked: {
                                    deleteDialog.libraryName = modelData.name
                                    deleteDialog.linkAddress = modelData.linkAddress
                                    deleteDialog.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ============ 状态栏（开发文档 8.1：结果计数 · 当前库名 · 在线状态）============
    footer: Rectangle {
        height: 28
        color: "#d8d8d8"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 12

            Text {
                id: statusText
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pixelSize: 12
                color: "#2d7a2d"
            }
            Text {
                text: root.resourcePage && root.libraryOpen
                      ? "共 " + resourcePageView.totalCount + " 条"
                      : ""
                font.pixelSize: 12
                color: "#555555"
            }
            Text {
                text: libraryService.currentName ? "库：" + libraryService.currentName : "未打开库"
                font.pixelSize: 12
                color: "#555555"
            }
            Text {
                // 依赖 root.libraryOpen（currentName 的 NOTIFY）驱动重算：
                // isOnline() 是方法调用不建绑定依赖，仅靠它自身不会随建库/切库刷新
                // 无库时无链接库可言，不显示在线状态（避免误读为"在线"）
                text: !root.libraryOpen ? ""
                      : (importService.isOnline() ? "● 在线" : "● 实体链接库离线")
                font.pixelSize: 12
                color: root.libraryOpen && !importService.isOnline() ? "#c0341d" : "#2d7a2d"
            }
        }
    }

    // 函数: 操作结果提示（状态栏，3.5 秒后淡出）
    function showStatus(message, isError) {
        statusText.text = message
        statusText.color = isError ? "#c0341d" : "#2d7a2d"
        statusTimer.restart()
    }

    Timer {
        id: statusTimer
        interval: 3500
        onTriggered: statusText.text = ""
    }

    CreateLibraryDialog {
        id: createDialog
        // 注入两个服务：属性名错开全局上下文属性名，避免同名遮蔽导致绑定自引用为空。
        libService: libraryService
        typePkgs: typePackageManager
        onCompleted: function (name) {
            root.showStatus("库「" + name + "」创建完成并已切换", false)
            root.resourcePage = true
        }
    }

    DeleteLibraryDialog {
        id: deleteDialog
        onConfirmed: function (deleteFiles) {
            // 显式取对话框属性，不依赖跨对象作用域解析 libraryName
            const libName = deleteDialog.libraryName
            const result = libraryService.deleteLibrary(libName, deleteFiles)
            if (result.ok)
                root.showStatus("库「" + libName + "」已删除"
                                + (deleteFiles ? "（含实体文件）" : "（实体文件已保留）"), false)
            else
                root.showStatus(result.message, true)
        }
    }

    ImportDialog {
        id: importDialog
    }

    SettingsDialog {
        id: settingsDialog
        // 保存成功由对话框 accept() 触发：
        // - 每页数量立即生效 → 资源页按新 pageSize 重查；
        // - 封面长边对已生成封面不重排，提示用户右键重新生成后生效。
        onAccepted: {
            root.showStatus("设置已保存（每页数量已生效；封面需重新生成后生效）", false)
            resourcePageView.reload()
        }
    }

    ViewerWindow {
        id: viewerWindow
        onOpenFailed: function (message) {
            root.showStatus(message, true)
        }
    }

    // 导出目标：选择空文件夹后原样复原包文件（开发文档 5.6）
    property int exportTargetId: 0
    FolderDialog {
        id: exportFolderPicker
        title: "导出到空文件夹"
        onAccepted: {
            var local = String(selectedFolder)
            if (local.indexOf("file:///") === 0)
                local = local.substring("file:///".length)
            local = decodeURIComponent(local) // %20 等还原为真实路径
            const result = importService.exportPackage(root.exportTargetId, local)
            if (result.ok)
                root.showStatus("已导出 " + result.count + " 个文件到：" + local, false)
            else
                root.showStatus(result.message, true)
        }
    }
}
