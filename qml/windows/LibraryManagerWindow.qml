// 库管理窗口（多进程模型下的门禁/临时管理界面）。
// 布局：左上库列表（点击库名拉起该库独立进程窗口；行尾 ⋯ 菜单），右侧上下分区
// （右上=图标+名称+版本占位，右下=新建库/打开本地仓库）。
// 建库/重命名/删除以本窗口内嵌 Popup 表单完成，不再使用独立 Dialog。
// 所有操作结果仅写控制台（console.log/warn），不设状态栏与提示链路。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: win

    width: 860
    height: 560
    minimumWidth: 640
    minimumHeight: 420
    color: "#181d27"
    flags: Qt.Window | Qt.FramelessWindowHint

    // 便捷查询：按库名取链接库地址（打开所在文件夹 / 删除确认展示用）
    function addressOf(name) {
        const list = libraryService.libraries
        for (let i = 0; i < list.length; ++i) {
            if (list[i].name === name)
                return list[i].linkAddress
        }
        return ""
    }

    function openFolder(name) {
        const address = addressOf(name)
        if (address === "")
            return
        // linkAddress 已存为 '/' 风格绝对路径；拼接 file:/// 交给系统打开
        Qt.openUrlExternally("file:///" + address)
    }

    // 把行尾菜单定位到锚点按钮正下方（Popup 无 Menu 的 popup(item) 重载）
    function placeRowMenu(anchor) {
        const pos = anchor.mapToItem(null, 0, 0)
        rowMenu.x = pos.x
        rowMenu.y = pos.y + anchor.height + 2
        rowMenu.open()
    }

    // 建库成功后的统一后续：拉起新库进程（创建后自动打开，原库不受影响）
    function launchAndReport(name) {
        const result = libraryService.launchLibrary(name)
        if (!result.ok)
            console.warn("[library] 打开新建库失败: " + result.message)
    }

    // ---- 自绘控件：下拉菜单项 ----
    component MenuItemRect: Rectangle {
        id: mi
        property string text: ""
        property bool enabled: true
        signal clicked
        Layout.fillWidth: true
        implicitHeight: 32
        radius: 5
        color: "transparent"
        Text {
            text: mi.text
            font.pixelSize: 13
            color: mi.enabled ? "#e6eaf2" : "#5c6472"
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
        }
        MouseArea {
            anchors.fill: parent
            enabled: mi.enabled
            hoverEnabled: true
            onEntered: mi.color = "#2a3346"
            onExited: mi.color = "transparent"
            onClicked: mi.clicked()
        }
    }

    // ============ 标题栏 ============
    Rectangle {
        id: titleBar
        width: parent.width
        height: 40
        color: "#141824"

        MouseArea {
            anchors.fill: parent
            onPressed: win.startSystemMove()
        }

        Text {
            text: "库管理"
            color: "#e6eaf2"
            font.pixelSize: 14
            font.bold: true
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            width: 32
            height: 32
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            color: "transparent"
            radius: 4
            Text {
                text: "✕"
                color: "#aab3c5"
                font.pixelSize: 14
                anchors.centerIn: parent
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: parent.color = "#2a3346"
                onExited: parent.color = "transparent"
                onClicked: win.close()
            }
        }
    }

    // ============ 主体：左列表 + 右信息 ============
    RowLayout {
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        // ---- 左：库列表 ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 500
            color: "#181d27"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Text {
                    text: libraryService.libraries.length === 0
                          ? "尚未创建任何库。点击右侧「新建库」或「打开本地仓库」开始。"
                          : "全部库（" + libraryService.libraries.length + "）"
                    color: "#aab3c5"
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                ListView {
                    id: libList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: libraryService.libraries

                    delegate: Rectangle {
                        id: libRow
                        required property var modelData
                        width: ListView.view.width
                        height: 64
                        radius: 6
                        color: nameArea.containsMouse || menuArea.containsMouse ? "#1e2634" : "#181d27"
                        border.width: 1
                        border.color: "#272f3d"

                        // 库名区：点击打开该库主窗口（新进程）
                        MouseArea {
                            id: nameArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                const result = libraryService.launchLibrary(libRow.modelData.name)
                                if (!result.ok)
                                    console.warn("[library] 打开库失败: " + result.message)
                            }
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.right: menuBtn.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                text: (libRow.modelData.isCurrent ? "● " : "○ ") + libRow.modelData.name
                                font.pixelSize: 15
                                font.bold: true
                                color: "#e6eaf2"
                            }
                            Text {
                                text: libRow.modelData.linkAddress
                                color: "#aab3c5"
                                font.pixelSize: 11
                                width: libRow.width - 60
                                elide: Text.ElideMiddle
                            }
                        }

                        // 行尾 ⋯ 菜单按钮
                        Rectangle {
                            id: menuBtn
                            width: 34
                            height: 34
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 4
                            color: menuArea.containsMouse ? "#2a3346" : "transparent"
                            Text {
                                text: "⋯"
                                color: "#aab3c5"
                                font.pixelSize: 16
                                anchors.centerIn: parent
                            }
                            MouseArea {
                                id: menuArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    rowMenu.libName = libRow.modelData.name
                                    win.placeRowMenu(menuBtn)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- 分隔线 ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: "#272f3d"
        }

        // ---- 右：信息 + 操作 ----
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#181d27"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                // 右上：图标 + 名称 + 版本（图标/版本暂占位留空）
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Rectangle {
                        id: appIcon
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                        radius: 12
                        color: "#1e2634"
                        border.width: 1
                        border.color: "#272f3d"
                        Text {
                            text: "J"
                            color: "#e6eaf2"
                            font.pixelSize: 26
                            font.bold: true
                            anchors.centerIn: parent
                        }
                    }
                    ColumnLayout {
                        spacing: 4
                        Text {
                            text: "Jarvis"
                            color: "#e6eaf2"
                            font.pixelSize: 20
                            font.bold: true
                        }
                        // 版本信息：暂留空
                        Text {
                            text: ""
                            color: "#aab3c5"
                            font.pixelSize: 12
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // 右下：新建库 / 打开本地仓库
                Button {
                    text: "新建库"
                    highlighted: true
                    Layout.fillWidth: true
                    onClicked: createForm.open()
                }
                Button {
                    text: "打开本地仓库"
                    Layout.fillWidth: true
                    onClicked: importFolderPicker.open()
                }
            }
        }
    }

    // ============ 行尾菜单：重命名/打开文件夹/删除/导出 ============
    Popup {
        id: rowMenu
        property string libName: ""
        width: 190
        padding: 5
        background: Rectangle {
            color: "#181d27"
            radius: 8
            border.color: "#272f3d"
            border.width: 1
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            MenuItemRect {
                text: "重命名库…"
                enabled: rowMenu.libName !== libraryService.currentName
                onClicked: {
                    rowMenu.close()
                    renameForm.libName = rowMenu.libName
                    renameTextField.text = rowMenu.libName
                    errLabel.text = ""
                    renameForm.open()
                }
            }
            MenuItemRect {
                text: "打开库所在文件夹"
                onClicked: { rowMenu.close(); win.openFolder(rowMenu.libName) }
            }
            MenuItemRect {
                text: "删除库…"
                enabled: rowMenu.libName !== libraryService.currentName
                onClicked: {
                    rowMenu.close()
                    deleteForm.libName = rowMenu.libName
                    deleteForm.open()
                }
            }
            MenuItemRect {
                text: "导出库…"
                onClicked: {
                    rowMenu.close()
                    exportPendingName = rowMenu.libName
                    exportFolderPicker.open()
                }
            }
        }
    }

    // ============ 新建库表单（内嵌 Popup，替代 CreateLibraryDialog） ============
    Popup {
        id: createForm
        modal: true
        x: (win.width - width) / 2
        y: (win.height - height) / 2
        width: 460
        padding: 16
        background: Rectangle {
            color: "#181d27"
            radius: 8
            border.color: "#272f3d"
            border.width: 1
        }

        function submit() {
            const forms = []
            for (let i = 0; i < typesRepeater.count; ++i) {
                const box = typesRepeater.itemAt(i)
                if (box.checked && forms.indexOf(box.form) === -1)
                    forms.push(box.form)
            }
            const result = libraryService.registerLibrary(
                nameField.text, linkField.text, forms)
            if (result && result.ok) {
                console.log("[library] 已新建库: " + result.name)
                createForm.close()
                createForm.reset()
                win.launchAndReport(result.name)
            } else {
                errText.text = result ? result.message : "创建调用未返回结果"
            }
        }

        function reset() {
            nameField.text = ""
            linkField.text = ""
            errText.text = ""
            for (let i = 0; i < typesRepeater.count; ++i)
                typesRepeater.itemAt(i).checked = true
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: "新建库"
                color: "#e6eaf2"
                font.pixelSize: 15
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: "库名："; Layout.preferredWidth: 72; color: "#aab3c5" }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "如 Main"
                    selectByMouse: true
                    onAccepted: createForm.submit()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: "链接库："; Layout.preferredWidth: 72; color: "#aab3c5" }
                TextField {
                    id: linkField
                    Layout.fillWidth: true
                    readOnly: true
                    placeholderText: "实体数据包存放目录"
                }
                Button {
                    text: "浏览…"
                    onClicked: linkFolderPicker.open()
                }
            }

            Label { text: "启用类型："; color: "#aab3c5" }

            Repeater {
                id: typesRepeater
                model: typePackageManager.loadedPackages
                delegate: CheckBox {
                    required property var modelData
                    text: modelData.displayName + "（" + modelData.form + "）"
                    property string form: modelData.form
                    checked: true
                    leftPadding: 18
                }
            }

            Text {
                id: errText
                Layout.fillWidth: true
                color: "#e5675f"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                visible: text !== ""
                text: ""
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "取消"; onClicked: createForm.close() }
                Button {
                    text: "创建"
                    highlighted: true
                    onClicked: createForm.submit()
                }
            }
        }

        onOpened: createForm.reset()
    }

    // ============ 重命名表单（内嵌 Popup） ============
    Popup {
        id: renameForm
        modal: true
        x: (win.width - width) / 2
        y: (win.height - height) / 2
        width: 380
        padding: 16
        property string libName: ""
        background: Rectangle {
            color: "#181d27"
            radius: 8
            border.color: "#272f3d"
            border.width: 1
        }

        function submit() {
            const result = libraryService.renameLibrary(renameForm.libName,
                                                       renameTextField.text)
            if (result.ok) {
                console.log("[library] 已重命名库: " + renameForm.libName + " → " + renameTextField.text)
                renameForm.close()
            } else {
                errLabel.text = result.message
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: "重命名库"
                color: "#e6eaf2"
                font.pixelSize: 15
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: "库名："; Layout.preferredWidth: 56; color: "#aab3c5" }
                TextField {
                    id: renameTextField
                    Layout.fillWidth: true
                    selectByMouse: true
                    onAccepted: renameForm.submit()
                }
            }

            Text {
                id: errLabel
                Layout.fillWidth: true
                color: "#e5675f"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                visible: text !== ""
                text: ""
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "取消"; onClicked: renameForm.close() }
                Button {
                    text: "确定"
                    highlighted: true
                    onClicked: renameForm.submit()
                }
            }
        }
    }

    // ============ 删除确认（内嵌 Popup，替代 DeleteLibraryDialog） ============
    Popup {
        id: deleteForm
        modal: true
        x: (win.width - width) / 2
        y: (win.height - height) / 2
        width: 420
        padding: 16
        property string libName: ""
        property bool deleteEntityFiles: false
        background: Rectangle {
            color: "#181d27"
            radius: 8
            border.color: "#272f3d"
            border.width: 1
        }

        function submit() {
            const result = libraryService.deleteLibrary(deleteForm.libName,
                                                        deleteForm.deleteEntityFiles)
            if (result.ok) {
                console.log("[library] 已删除库: " + deleteForm.libName
                            + (deleteForm.deleteEntityFiles ? "（含实体文件）" : "（保留实体文件）"))
                deleteForm.close()
            } else {
                console.warn("[library] 删除库失败: " + result.message)
                deleteForm.close()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: "删除库「" + deleteForm.libName + "」"
                color: "#e6eaf2"
                font.pixelSize: 15
                font.bold: true
            }

            Text {
                text: "链接库目录：" + win.addressOf(deleteForm.libName)
                color: "#aab3c5"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            CheckBox {
                text: "同时删除链接库中的实体文件（不可恢复）"
                checked: false
                onToggled: deleteForm.deleteEntityFiles = checked
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "取消"; onClicked: deleteForm.close() }
                Button {
                    text: "确认删除"
                    highlighted: true
                    onClicked: deleteForm.submit()
                }
            }
        }
    }

    // ============ 文件夹选择器 ============
    FolderDialog {
        id: linkFolderPicker
        title: "选择链接库目录"
        onAccepted: {
            linkField.text = decodeURIComponent(
                selectedFolder.toString().replace(/^file:\/\/\//, ""))
        }
    }

    FolderDialog {
        id: importFolderPicker
        title: "选择已导出的库目录（含 library.json）"
        onAccepted: {
            const result = libraryService.importLibrary(decodeURIComponent(
                selectedFolder.toString().replace(/^file:\/\/\//, "")))
            if (result.ok)
                console.log("[library] 已导入库")
            else
                console.warn("[library] 导入库失败: " + (result.message || "未知错误"))
        }
    }

    property string exportPendingName: ""
    FolderDialog {
        id: exportFolderPicker
        title: "选择导出目标目录"
        onAccepted: {
            const result = libraryService.exportLibrary(
                exportPendingName,
                decodeURIComponent(selectedFolder.toString().replace(/^file:\/\/\//, "")))
            if (result.ok)
                console.log("[library] 已导出库: " + exportPendingName)
            else
                console.warn("[library] 导出库失败: " + (result.message || "未知错误"))
        }
    }
}