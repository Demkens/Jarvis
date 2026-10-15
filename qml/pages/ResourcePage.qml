// 资源显示页
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../dialogs"

Item {
    id: page

    // 信号: 与 Main.qml 的协作信号
    signal openViewer(int packageId)
    signal exportPackage(int packageId)
    // 打开详情窗口：DetailWindow 提升到 Main.qml 顶层（Window 不能挂在深层 Item 下，
    // 否则弹窗不可见——见 M4 右键"打开详情"无反应问题），此处只转发 id。
    signal openDetail(int packageId)
    // 打开元数据编辑弹窗（本页只转发 id，实例与写库编排在 AppDialogs）
    signal editPackage(int packageId)

    // 查询状态（JS 数组整体替换以触发模型刷新，避免 Qt 对原地 push 无通知的坑）
    property var viewItems: []
    property int totalCount: 0
    property int pageCount: 1
    property int currentPage: 1

    // 筛选条件（唯一持有者即本页；维度由统一筛选弹窗 FilterDialog 编辑后回填）
    // tagIds：标签筛选（空=不限）；tagMatchAll：多标签组合，false=OR / true=AND
    property var filters: ({
        form: "",
        creationFrom: "",
        creationTo: "",
        createdFrom: "",
        createdTo: "",
        tagIds: [],
        tagMatchAll: false
    })

    // 重新查询当前页
    function reload() {
        if (!libraryService.currentName)
            return
        var result = typeEngine.queryPackages({
            form: page.filters.form,
            keyword: searchField.text,
            scope: scopeCombo.currentIndex,
            creationFrom: page.filters.creationFrom,
            creationTo: page.filters.creationTo,
            createdFrom: page.filters.createdFrom,
            createdTo: page.filters.createdTo,
            tagIds: page.filters.tagIds || [],
            tagMatchAll: !!page.filters.tagMatchAll
        }, page.currentPage)
        if (!result.ok) {
            console.warn("[resource] 查询失败：" + (result.message || "未知错误"))
            return
        }
        // 深拷贝为纯 JS 数组（核心返回对象不宜原地改）
        page.viewItems = JSON.parse(JSON.stringify(result.items))
        page.totalCount = result.total
        page.pageCount = Math.max(1, result.pages)
        // 翻页越界保护：当前页超出新页数则回退到末页重查
        if (page.currentPage > page.pageCount) {
            page.currentPage = page.pageCount
            page.reload()
            return
        }
        layoutTimer.restart()
    }

    function openItem(modelData) { page.openViewer(Number(modelData.id)) }

    // 瀑布流重排：按"当前最短列"投放，列宽随可用宽度自适应。
    // 因封面 AR 已随资源入库，无需等图片加载即可定高 → 布局稳定、不跳变、不重叠。
    function relayout() {
        var items = page.viewItems
        var n = items.length
        if (n === 0) {
            masonryHost.height = 0
            return
        }
        var hostW = masonryHost.width
        if (hostW <= 0)
            return
        var gap = 12
        var targetColW = 200
        var cols = Math.max(1, Math.floor((hostW + gap) / (targetColW + gap)))
        var colW = (hostW - gap * (cols - 1)) / cols
        var heights = []
        for (var c = 0; c < cols; ++c)
            heights.push(0)
        for (var i = 0; i < n; ++i) {
            var col = 0
            for (var k = 1; k < cols; ++k)
                if (heights[k] < heights[col])
                    col = k
            var ar = items[i].aspect_ratio
            if (!ar || ar <= 0)
                ar = 1.0
            var h = Math.round(colW / ar)
            var it = cardRepeater.itemAt(i)
            if (it) {
                it.cardWidth = colW
                it.cardHeight = h
                it.x = Math.round(col * (colW + gap))
                it.y = Math.round(heights[col])
            }
            heights[col] += h + gap
        }
        var maxH = 0
        for (var m = 0; m < cols; ++m)
            if (heights[m] > maxH)
                maxH = heights[m]
        masonryHost.height = Math.max(0, maxH - gap)
    }

    // 合并同一帧内的多次重排请求（模型变化/宽度变化）
    Timer {
        id: layoutTimer
        interval: 0
        onTriggered: page.relayout()
    }

    // 类型下拉模型：首位"全部类型" + 已启用类型（enabledTypes 变化自动重算）
    // 用途：注入统一筛选弹窗（FilterDialog）的类型维度
    readonly property var typeFilterModel: [{"form": "", "displayName": "全部类型"}].concat(typeEngine.enabledTypes)

    Component.onCompleted: {
        if (libraryService.currentName) page.reload()
    }

    Connections {
        target: typeEngine
        function onPackagesChanged() { page.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 搜索 / 类型筛选
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 16
            spacing: 8

            // 搜索范围：0=标题（默认） / 1=创作者（含别名），切换即时生效
            ComboBox {
                id: scopeCombo
                Layout.preferredWidth: 110
                model: ["标题", "创作者"]
                currentIndex: 0
                onActivated: {
                    page.currentPage = 1
                    page.reload()
                }
            }

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

            // 统一筛选入口：类型 / 创作日期区间 / 入库时间区间（含二期标签树）集中在
            // FilterDialog 内编辑，本按钮只负责唤起并回填结果
            Rectangle {
                id: filterButton
                Layout.preferredWidth: 76
                Layout.preferredHeight: 32
                radius: 4
                color: "#86b2ff"
                Text {
                    anchors.centerIn: parent
                    text: "筛选"
                    color: "#ffffff"
                    font.pixelSize: 13
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: filterDialog.openWith(page.filters)
                }
            }
        }

        // 空态（有库但无数据）
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.totalCount === 0 && libraryService.currentName !== ""
            color: "transparent"
            Text {
                anchors.centerIn: parent
                text: "库无文件，待导入"
                font.pixelSize: 16
            }
        }

        // 内容视图（瀑布流：常态仅封面；悬停放大 + 底部信息浮层）
        Flickable {
            id: masonry
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            visible: page.totalCount > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: masonryHost.height

            Item {
                id: masonryHost
                width: masonry.width
                height: 0

                Repeater {
                    id: cardRepeater
                    model: page.viewItems
                    delegate: cardDelegate
                    onItemAdded: layoutTimer.restart()
                    onCountChanged: layoutTimer.restart()
                }
            }

            onWidthChanged: layoutTimer.restart()
        }

        // 分页器
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 16
            Layout.topMargin: 4
            visible: page.totalCount > 0
            spacing: 6

            Text {
                text: "共 " + page.totalCount + " 条"
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

    // 瀑布流卡片：常态仅封面；悬停轻微放大 + 底部半透明浮层（仅标题/作者）
    Component {
        id: cardDelegate
        Rectangle {
            property real cardWidth: 200
            property real cardHeight: 200
            width: cardWidth
            height: cardHeight
            radius: 8
            clip: true

            // 容器比例 = 图片比例（AR 已入库）→ 完整显示且无留白
            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                source: modelData.coverUrl || ""
            }

            // 悬停：轻微放大并提升层级，避免被相邻卡片压住
            HoverHandler {
                id: cardHover
            }
            scale: cardHover.hovered ? 1.04 : 1.0
            z: cardHover.hovered ? 2 : 0
            Behavior on scale {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }

            // 底部半透明浮层：仅「标题」「作者」
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Math.min(parent.height, 46)
                color: "#a6000000"
                opacity: cardHover.hovered ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    Text {
                        width: parent.width
                        text: modelData.title || "untitled"
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: modelData.creatorName || "佚名"
                        color: "#d5dbe6"
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }

            // 二级交互：双击=查看器，右键=操作菜单
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    if (mouse.button === Qt.RightButton) {
                        contextMenu.currentItem = modelData
                        contextMenu.popup()
                    }
                }
                onDoubleClicked: page.openItem(modelData)
            }
        }
    }

    // 右键操作菜单
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
                    console.warn("[resource] 实体目录不可达（库离线？）")
            }
        }
        MenuItem {
            text: "用系统方式打开"
            enabled: {
                if (!contextMenu.currentItem)
                    return false
                return typeEngine.firstFileUrl(Number(contextMenu.currentItem.id)).toString() !== ""
            }
            onTriggered: {
                if (!contextMenu.currentItem)
                    return
                var u = typeEngine.firstFileUrl(Number(contextMenu.currentItem.id))
                if (u.toString() !== "")
                    Qt.openUrlExternally(u)
                else
                    console.warn("[resource] 无文件可打开（库离线或包为空？）")
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
            text: "编辑元数据…"
            onTriggered: {
                if (contextMenu.currentItem)
                    page.editPackage(Number(contextMenu.currentItem.id))
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
                            if (r.ok)
                                console.log("[resource] 已删除 #" + deleteConfirm.confirmId)
                            else
                                console.warn("[resource] 删除失败：" + (r.message || ""))
                            deleteConfirm.close()
                        }
                    }
                }
            }
        }
    }

    // 页码计算（窗口式：1 … 前/当前/后 … 末）
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

    // 统一筛选弹窗：筛选状态由本页持有（page.filters），弹窗只负责编辑与回填
    FilterDialog {
        id: filterDialog
        typeModel: page.typeFilterModel
        onApplied: function (filters) {
            page.filters = filters
            page.currentPage = 1
            page.reload()
        }
    }
}