import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// M4 资源显示页（开发文档 8.2）：网格/列表视图、标题搜索、类型筛选、分页、
// 单击预览面板（改标题/创作者/评分）、双击进查看器、右键操作、空态引导。
// 数据源全部经 TypeEngine 门面（queryPackages / readFields / writeFields /
// updateTitle / ratePackage / deletePackage / regenerateCover / packageDir）。
Item {
    id: page

    // 与 Main.qml 的协作信号
    signal openViewer(int packageId)
    signal requestImport()
    signal exportPackage(int packageId)
    // 打开详情窗口：DetailWindow 提升到 Main.qml 顶层（Window 不能挂在深层 Item 下，
    // 否则弹窗不可见——见 M4 右键"打开详情"无反应问题），此处只转发 id。
    signal openDetail(int packageId)

    // ---- 查询状态（JS 数组整体替换以触发模型刷新，避免 Qt 对原地 push 无通知的坑）----
    property var viewItems: []
    property int totalCount: 0
    property int pageCount: 1
    property int currentPage: 1
    property string currentForm: ""
    property bool gridMode: true
    // 封面缓存破坏戳：封面文件可能被重新生成（设置长边/右键重生成），
    // 给 coverUrl 追加 ?v=N 让 QML Image 因 URL 变化强制重新加载（toLocalFile 会忽略 query，加载不受影响）
    property int coverStamp: 0

    // 预览面板状态
    property int previewId: 0
    property var previewDetail: ({})

    // 操作结果提示（右上角短暂显示）
    property string toastText: ""

    Timer {
        id: toastTimer
        interval: 3500
        onTriggered: page.toastText = ""
    }

    function showToast(text) {
        page.toastText = text
        toastTimer.restart()
    }

    function formatBytes(bytes) {
        if (!bytes)
            return "0 B"
        if (bytes < 1024)
            return bytes + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        return (bytes / 1024 / 1024).toFixed(2) + " MB"
    }

    function formatTime(iso) {
        if (!iso)
            return "—"
        var s = String(iso)
        return s.length >= 10 ? s.slice(0, 10) + " " + s.slice(11, 16) : s
    }

    // 类型 form → 显示名（enabledTypes 里有 displayName；找不到回退 form）
    function formDisplayName(form) {
        if (!form)
            return "—"
        var list = typeEngine.enabledTypes
        for (var i = 0; i < list.length; ++i) {
            if (list[i].form === form)
                return list[i].displayName
        }
        return form
    }

    // 重新查询当前页（搜索/筛选/翻页/包事件/设置保存后调用）
    function reload() {
        if (!libraryService.currentName)
            return
        var result = typeEngine.queryPackages(page.currentForm, searchField.text, page.currentPage)
        if (!result.ok) {
            page.showToast("查询失败：" + (result.message || "未知错误"))
            return
        }
        page.coverStamp += 1
        // 深拷贝为纯 JS 数组（核心返回对象不宜原地改），给每项 coverUrl 加版本戳强制封面重载
        var items = JSON.parse(JSON.stringify(result.items))
        for (var i = 0; i < items.length; ++i) {
            if (items[i].coverUrl)
                items[i].coverUrl = String(items[i].coverUrl) + "?v=" + page.coverStamp
        }
        page.viewItems = items
        page.totalCount = result.total
        page.pageCount = Math.max(1, result.pages)
        // 翻页越界保护：当前页超出新页数则回退到末页重查
        if (page.currentPage > page.pageCount) {
            page.currentPage = page.pageCount
            page.reload()
            return
        }
        // 刷新预览面板（若预览的包仍存在；封面同加版本戳）
        if (page.previewId > 0) {
            var detail = JSON.parse(JSON.stringify(typeEngine.readFields(page.previewId)))
            if (detail && detail.coverUrl)
                detail.coverUrl = String(detail.coverUrl) + "?v=" + page.coverStamp
            page.previewDetail = detail
        }
    }

    function selectItem(modelData) {
        // 切换预览对象前先提交未保存的编辑（onEditingFinished 只在回车/失焦触发，
        // 直接点其他卡片时不会触发，导致"改了没保存"）
        if (page.previewId > 0) {
            if (titleField.text !== (page.previewDetail.title || ""))
                typeEngine.updateTitle(page.previewId, titleField.text)
            if (creatorField.text !== (page.previewDetail.creatorName || ""))
                typeEngine.writeFields(page.previewId, {"creator_id": creatorField.text})
        }
        var id = Number(modelData.id)
        page.previewId = id
        page.previewDetail = typeEngine.readFields(id)
    }

    function openItem(modelData) {
        page.openViewer(Number(modelData.id))
    }

    // 类型 ComboBox 模型：首位"全部类型" + 已启用类型（enabledTypes 变化自动重算）
    readonly property var typeFilterModel: [{"form": "", "displayName": "全部类型"}].concat(typeEngine.enabledTypes)

    Component.onCompleted: {
        // 库已打开时进入页面立即查询；类型下拉默认"全部类型"
        if (libraryService.currentName)
            page.reload()
        if (typeCombo.count > 0)
            typeCombo.currentIndex = 0
    }

    Connections {
        target: typeEngine
        function onPackagesChanged() {
            page.reload()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ============ 主内容区 ============
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- 顶行：搜索 / 类型筛选 / 视图切换 / 刷新 ----
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 10
                spacing: 8

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: "按标题搜索…"
                    selectByMouse: true
                    // 防抖：停止输入 300ms 后查询，回到第一页
                    Timer {
                        id: searchTimer
                        interval: 300
                        onTriggered: {
                            page.currentPage = 1
                            page.reload()
                        }
                    }
                    onTextChanged: searchTimer.restart()
                }

                ComboBox {
                    id: typeCombo
                    Layout.preferredWidth: 150
                    model: page.typeFilterModel
                    textRole: "displayName"
                    onActivated: {
                        page.currentForm = currentValue.form
                        page.currentPage = 1
                        page.reload()
                    }
                }

                // 视图切换（网格 / 列表）——自定义按钮避免原生样式限制
                Rectangle {
                    Layout.preferredWidth: 68
                    Layout.preferredHeight: 34
                    radius: 4
                    color: page.gridMode ? "#3d6ecd" : "#e6eaf2"
                    Text {
                        anchors.centerIn: parent
                        text: "网格"
                        color: page.gridMode ? "#ffffff" : "#2b2b2b"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.gridMode = true
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 68
                    Layout.preferredHeight: 34
                    radius: 4
                    color: !page.gridMode ? "#3d6ecd" : "#e6eaf2"
                    Text {
                        anchors.centerIn: parent
                        text: "列表"
                        color: !page.gridMode ? "#ffffff" : "#2b2b2b"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.gridMode = false
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 68
                    Layout.preferredHeight: 34
                    radius: 4
                    color: "#e6eaf2"
                    Text {
                        anchors.centerIn: parent
                        text: "刷新"
                        color: "#2b2b2b"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.reload()
                    }
                }

                // 导入入口（顶栏"新建库"常驻后，导入按钮落在资源页工具栏）
                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 34
                    radius: 4
                    color: "#3d6ecd"
                    Text {
                        anchors.centerIn: parent
                        text: "导入数据包"
                        color: "#ffffff"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.requestImport()
                    }
                }
            }

            // ---- 操作结果提示 ----
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 4
                visible: page.toastText !== ""
                text: page.toastText
                color: "#c0392b"
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }

            // ---- 空态（有库但无数据）----
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: page.totalCount === 0 && libraryService.currentName !== ""
                color: "transparent"
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 12
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "库里还没有数据包"
                        color: "#888888"
                        font.pixelSize: 16
                    }
                    Rectangle {
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 36
                        radius: 5
                        color: "#3d6ecd"
                        Text {
                            anchors.centerIn: parent
                            text: "导入数据包"
                            color: "#ffffff"
                            font.pixelSize: 14
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.requestImport()
                        }
                    }
                }
            }

            // ---- 内容视图（网格 / 列表）----
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: page.totalCount > 0
                clip: true

                GridView {
                    id: gridView
                    anchors.fill: parent
                    visible: page.gridMode
                    model: page.viewItems
                    cellWidth: 210
                    cellHeight: 250
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: cardDelegate
                }

                ListView {
                    id: listView
                    anchors.fill: parent
                    visible: !page.gridMode
                    model: page.viewItems
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: listDelegate
                }
            }

            // ---- 分页器 ----
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 8
                Layout.topMargin: 6
                visible: page.totalCount > 0
                spacing: 6

                Text {
                    text: "共 " + page.totalCount + " 条"
                    color: "#666666"
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 28
                    radius: 4
                    color: page.currentPage > 1 ? "#e6eaf2" : "#f0f0f0"
                    Text {
                        anchors.centerIn: parent
                        text: "上一页"
                        color: page.currentPage > 1 ? "#2b2b2b" : "#aaaaaa"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: page.currentPage > 1
                        onClicked: {
                            page.currentPage -= 1
                            page.reload()
                        }
                    }
                }

                // 页码（窗口式：1 … 前/当前/后 … 末）
                Row {
                    id: pageRow
                    Layout.preferredHeight: 28
                    spacing: 4
                    Repeater {
                        model: page.pageNumbers()
                        delegate: Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: modelData === page.currentPage ? "#3d6ecd" : "#e6eaf2"
                            visible: modelData !== 0 // 0 表示省略号占位
                            Text {
                                anchors.centerIn: parent
                                text: modelData === 0 ? "…" : modelData
                                color: modelData === page.currentPage ? "#ffffff" : "#2b2b2b"
                                font.pixelSize: 12
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (modelData !== page.currentPage) {
                                        page.currentPage = modelData
                                        page.reload()
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 28
                    radius: 4
                    color: page.currentPage < page.pageCount ? "#e6eaf2" : "#f0f0f0"
                    Text {
                        anchors.centerIn: parent
                        text: "下一页"
                        color: page.currentPage < page.pageCount ? "#2b2b2b" : "#aaaaaa"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: page.currentPage < page.pageCount
                        onClicked: {
                            page.currentPage += 1
                            page.reload()
                        }
                    }
                }
            }
        }

        // ============ 预览面板（单击卡片时显示）============
        Rectangle {
            id: previewPanel
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            visible: page.previewId > 0
            color: "#f7f8fb"
            border.color: "#e0e4ec"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "预览"
                        color: "#2b2b2b"
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Rectangle {
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 26
                        radius: 4
                        color: "#e6eaf2"
                        Text {
                            anchors.centerIn: parent
                            text: "关闭"
                            color: "#2b2b2b"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.previewId = 0
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 140
                    Layout.alignment: Qt.AlignHCenter
                    radius: 6
                    color: "#eef1f6"
                    clip: true
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: page.previewDetail.coverUrl || ""
                    }
                }

                // 标题（可编辑，回车/失焦提交）
                Text { text: "标题"; color: "#888888"; font.pixelSize: 11 }
                TextField {
                    id: titleField
                    Layout.fillWidth: true
                    text: page.previewDetail.title || ""
                    selectByMouse: true
                    onEditingFinished: {
                        var r = typeEngine.updateTitle(page.previewId, text)
                        if (!r.ok)
                            page.showToast("标题修改失败：" + (r.message || ""))
                        else
                            page.showToast("标题已更新")
                    }
                }

                // 创作者（可编辑，find-or-create 语义由核心处理）
                Text { text: "创作者"; color: "#888888"; font.pixelSize: 11 }
                TextField {
                    id: creatorField
                    Layout.fillWidth: true
                    text: page.previewDetail.creatorName || ""
                    selectByMouse: true
                    onEditingFinished: {
                        var r = typeEngine.writeFields(page.previewId, {"creator_id": text})
                        if (!r.ok)
                            page.showToast("创作者修改失败：" + (r.message || ""))
                        else
                            page.showToast("创作者已更新")
                    }
                }

                // 评分（0–100，0=未评分；二次评分 0.4/0.6 由核心计算）
                Text { text: "评分"; color: "#888888"; font.pixelSize: 11 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    SpinBox {
                        id: scoreBox
                        from: 0
                        to: 100
                        editable: true
                        value: page.previewDetail.score || 0
                    }
                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 30
                        radius: 4
                        color: "#3d6ecd"
                        Text {
                            anchors.centerIn: parent
                            text: "保存"
                            color: "#ffffff"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                // editable SpinBox 输入未回车时 value 未提交，
                                // 先强制失焦提交，避免保存到旧值
                                scoreBox.focus = false
                                var r = typeEngine.ratePackage(page.previewId, scoreBox.value)
                                if (!r.ok)
                                    page.showToast("评分失败：" + (r.message || ""))
                                else
                                    page.showToast("评分已保存（加权 0.4/0.6）")
                            }
                        }
                    }
                }

                // 只读信息
                Text { text: "类型：" + page.formDisplayName(page.previewDetail.type_form); color: "#444444"; font.pixelSize: 12 }
                Text { text: "创作日期：" + (page.previewDetail.creation_date || "—"); color: "#444444"; font.pixelSize: 12 }
                Text { text: "大小：" + page.formatBytes(page.previewDetail.size); color: "#444444"; font.pixelSize: 12 }
                Text { text: "入库：" + page.formatTime(page.previewDetail.created_time); color: "#444444"; font.pixelSize: 12 }
                Text { text: "更新：" + page.formatTime(page.previewDetail.updated_time); color: "#444444"; font.pixelSize: 12 }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // ============ 网格卡片 ============
    Component {
        id: cardDelegate
        Rectangle {
            width: 198
            height: 240
            radius: 8
            color: "#ffffff"
            border.color: "#e0e4ec"
            clip: true

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Rectangle {
                    width: parent.width
                    height: 150
                    radius: 5
                    color: "#eef1f6"
                    clip: true
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: modelData.coverUrl || ""
                    }
                }

                Text {
                    width: parent.width
                    text: modelData.title || "untitled"
                    elide: Text.ElideRight
                    color: "#2b2b2b"
                    font.pixelSize: 13
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: (modelData.creatorName || "anonymous")
                          + "  ·  " + (modelData.type_form || "")
                    elide: Text.ElideRight
                    color: "#888888"
                    font.pixelSize: 11
                }
                Text {
                    width: parent.width
                    text: modelData.score > 0 ? "评分 " + modelData.score
                                             : page.formatBytes(modelData.size)
                    color: "#666666"
                    font.pixelSize: 11
                }
            }

            // 三级交互：单击=预览，双击=查看器，右键=操作菜单
            MouseArea {
                id: cardMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    if (mouse.button === Qt.RightButton) {
                        contextMenu.currentItem = modelData
                        contextMenu.popup()
                    } else {
                        page.selectItem(modelData)
                    }
                }
                onDoubleClicked: page.openItem(modelData)
            }
        }
    }

    // ============ 列表行 ============
    Component {
        id: listDelegate
        Rectangle {
            width: parent ? parent.width : 0
            height: 64
            color: "#ffffff"
            border.color: "#eef1f6"
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 48
                    radius: 4
                    color: "#eef1f6"
                    clip: true
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: modelData.coverUrl || ""
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: modelData.title || "untitled"
                        elide: Text.ElideRight
                        color: "#2b2b2b"
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (modelData.creatorName || "anonymous")
                              + "  ·  " + (modelData.type_form || "")
                        elide: Text.ElideRight
                        color: "#888888"
                        font.pixelSize: 11
                    }
                }
                Text {
                    Layout.preferredWidth: 70
                    text: modelData.score > 0 ? "评分 " + modelData.score : "未评分"
                    color: "#666666"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                }
                Text {
                    Layout.preferredWidth: 80
                    text: page.formatBytes(modelData.size)
                    color: "#666666"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    if (mouse.button === Qt.RightButton) {
                        contextMenu.currentItem = modelData
                        contextMenu.popup()
                    } else {
                        page.selectItem(modelData)
                    }
                }
                onDoubleClicked: page.openItem(modelData)
            }
        }
    }

    // ============ 右键操作菜单 ============
    Menu {
        id: contextMenu
        property var currentItem: null

        MenuItem {
            text: "打开详情"
            onTriggered: {
                if (contextMenu.currentItem)
                    page.openDetail(Number(contextMenu.currentItem.id))
            }
        }
        MenuItem {
            text: "重新生成封面"
            onTriggered: {
                if (!contextMenu.currentItem)
                    return
                var r = typeEngine.regenerateCover(Number(contextMenu.currentItem.id))
                if (r.ok) {
                    page.showToast("封面已重新生成")
                    page.reload() // 封面文件已变：重查并加版本戳，强制列表/预览显示新封面
                } else {
                    page.showToast("封面生成失败：" + (r.message || ""))
                }
            }
        }
        MenuItem {
            text: "打开实体位置"
            enabled: {
                if (!contextMenu.currentItem)
                    return false
                return typeEngine.packageDir(Number(contextMenu.currentItem.id)) !== ""
            }
            onTriggered: {
                if (!contextMenu.currentItem)
                    return
                var dir = typeEngine.packageDir(Number(contextMenu.currentItem.id))
                if (dir)
                    Qt.openUrlExternally("file:///" + encodeURIComponent(dir).replace(/%2F/gi, "/"))
                else
                    page.showToast("实体目录不可达（库离线？）")
            }
        }
        MenuItem {
            text: "导出到文件夹"
            onTriggered: {
                if (contextMenu.currentItem)
                    page.exportPackage(Number(contextMenu.currentItem.id))
            }
        }
        MenuItem {
            text: "删除"
            onTriggered: {
                if (!contextMenu.currentItem)
                    return
                deleteConfirm.confirmId = Number(contextMenu.currentItem.id)
                deleteConfirm.open()
            }
        }
    }

    // 删除确认
    Dialog {
        id: deleteConfirm
        property int confirmId: 0
        title: "删除数据包"
        standardButtons: Dialog.Cancel
        modal: true
        // Dialog 顶层弹出默认居中；显式固定宽度避免 contentItem 隐式宽度绑定循环
        width: 460

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "将删除该数据包（数据库记录、实体文件与封面），且不可恢复。确定继续？"
                wrapMode: Text.WrapAnywhere
                color: "#2b2b2b"
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 30
                    radius: 4
                    color: "#e6eaf2"
                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        color: "#2b2b2b"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: deleteConfirm.close()
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 30
                    radius: 4
                    color: "#c0392b"
                    Text {
                        anchors.centerIn: parent
                        text: "删除"
                        color: "#ffffff"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            var r = typeEngine.deletePackage(deleteConfirm.confirmId)
                            page.showToast(r.ok ? "已删除" : "删除失败：" + (r.message || ""))
                            if (page.previewId === deleteConfirm.confirmId)
                                page.previewId = 0
                            deleteConfirm.close()
                        }
                    }
                }
            }
        }
    }

    // ============ 页码计算（窗口式：1 … 前/当前/后 … 末）============
    function pageNumbers() {
        var pages = []
        var total = page.pageCount
        var cur = page.currentPage
        if (total <= 7) {
            for (var i = 1; i <= total; ++i)
                pages.push(i)
            return pages
        }
        pages.push(1)
        if (cur > 3)
            pages.push(0) // 省略号
        var start = Math.max(2, cur - 1)
        var end = Math.min(total - 1, cur + 1)
        for (var j = start; j <= end; ++j)
            pages.push(j)
        if (cur < total - 2)
            pages.push(0)
        pages.push(total)
        return pages
    }
}
