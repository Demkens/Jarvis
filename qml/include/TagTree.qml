// 标签树（M6）：扁平模型 + depth 缩进渲染（避免 QML 递归组件）。
// 数据源：typeEngine.tagTree（DFS 先序，[{id,parent_id,name,depth}]；parent_id=0 为顶级）。
// 两种形态：
//  - selectable=true（打标/筛选态）：渲染复选框，多选；
//      其中 enforceAntichain=true 时集合恒为反链——勾选某节点会自动取消其已勾选的祖先，
//      且「已勾选标签的祖先」灰显不可勾；
//      enforceAntichain=false（筛选态）时不作反链约束，自由多选。
//  - selectable=false（管理态）：不渲染复选框，单击行即选中（供重命名/建子标签/删除定位）。
// 对外属性：tagModel / checkedIds（可写，写入即整体替换）/ enforceAntichain / selectable /
//           currentId（管理态当前选中 id）/ rowHeight
// 对外信号：selectionChanged(var checkedIds) —— 打标/筛选态选择变化；
//           currentChanged(int id) —— 管理态选中行变化。
import QtQuick

Flickable {
    id: tree

    // 标签扁平模型（DFS 先序）
    property var tagModel: []
    // 已勾选的标签 id 列表（打标/筛选态）
    property var checkedIds: []
    // 是否强制反链（打标态 true；筛选态 false）
    property bool enforceAntichain: false
    // 是否渲染复选框（false = 管理态单选）
    property bool selectable: true
    // 管理态当前选中标签 id
    property int currentId: 0
    // 行高
    property int rowHeight: 26
    // 折叠（收起子节点）的标签 id 列表（整体替换以触发重算）
    property var collapsedIds: []

    // 当前可见行（依赖 tagModel/checkedIds/collapsedIds/selectable/currentId 自动重算）
    readonly property var rows: tree.buildRows()

    // 输出: 勾选集合变化（打标/筛选态）
    signal selectionChanged(var checkedIds)
    // 输出: 管理态选中行变化
    signal currentChanged(int id)

    function _byId() {
        var model = tree.tagModel || []
        var byId = {}
        for (var i = 0; i < model.length; ++i)
            byId[String(model[i].id)] = model[i]
        return byId
    }

    // 函数: id 的全部祖先 id 集合（不含自身；键为字符串）
    // 输入: id —— 标签 id；输出: {祖先id: true}
    function _ancestorSet(id) {
        var byId = tree._byId()
        var set = {}
        var p = byId[String(id)] ? byId[String(id)].parent_id : 0
        var guard = 0
        while (p > 0 && guard++ < 1024) {
            set[String(p)] = true
            p = byId[String(p)] ? byId[String(p)].parent_id : 0
        }
        return set
    }

    // 函数: 反链模式下被禁勾的集合 = 已勾选标签的全部祖先（键为字符串）
    // 输入/输出: 无
    function _disabledSet() {
        var set = {}
        if (!tree.enforceAntichain)
            return set
        for (var i = 0; i < tree.checkedIds.length; ++i) {
            var ancestors = tree._ancestorSet(tree.checkedIds[i])
            for (var key in ancestors)
                set[key] = true
        }
        return set
    }

    // 函数: 按模型顺序归一化 id 列表（保证输出顺序稳定）
    // 输入: ids —— 任意顺序的 id 数组；输出: 按 tagModel 先序排列的 id 数组
    function _sortByModel(ids) {
        var want = {}
        for (var i = 0; i < ids.length; ++i)
            want[String(ids[i])] = true
        var out = []
        var model = tree.tagModel || []
        for (var j = 0; j < model.length; ++j) {
            if (want[String(model[j].id)])
                out.push(model[j].id)
        }
        return out
    }

    // 函数: 计算当前可见行（折叠节点的子孙不显示），并附带 checked/disabled/current
    // 输入/输出: 无；输出: [{id,name,depth,hasChild,expanded,checked,disabled,current}]
    function buildRows() {
        var model = tree.tagModel || []
        var byId = tree._byId()
        var collapsed = {}
        for (var c = 0; c < tree.collapsedIds.length; ++c)
            collapsed[String(tree.collapsedIds[c])] = true
        var checked = {}
        for (var k = 0; k < tree.checkedIds.length; ++k)
            checked[String(tree.checkedIds[k])] = true
        var disabled = tree._disabledSet()
        var hasChild = {}
        for (var j = 0; j < model.length; ++j) {
            var parent = model[j].parent_id
            if (parent > 0)
                hasChild[String(parent)] = true
        }

        var out = []
        for (var n = 0; n < model.length; ++n) {
            var node = model[n]
            // 任一祖先折叠 → 本行隐藏
            var hidden = false
            var ancestor = node.parent_id
            var guard = 0
            while (ancestor > 0 && guard++ < 1024) {
                if (collapsed[String(ancestor)]) {
                    hidden = true
                    break
                }
                ancestor = byId[String(ancestor)] ? byId[String(ancestor)].parent_id : 0
            }
            if (hidden)
                continue
            out.push({
                          id: node.id,
                          name: node.name,
                          depth: node.depth,
                          hasChild: !!hasChild[String(node.id)],
                          expanded: !collapsed[String(node.id)],
                          checked: !!checked[String(node.id)],
                          disabled: !!disabled[String(node.id)],
                          current: !tree.selectable && tree.currentId === node.id
                      })
        }
        return out
    }

    // 函数: 展开/收起某节点
    // 输入: id —— 标签 id；输出: 无
    function toggleCollapse(id) {
        var next = []
        var present = false
        for (var i = 0; i < tree.collapsedIds.length; ++i) {
            if (tree.collapsedIds[i] === id) {
                present = true
                continue
            }
            next.push(tree.collapsedIds[i])
        }
        if (!present)
            next.push(id)
        tree.collapsedIds = next
    }

    // 函数: 打标/筛选态勾选切换；反链模式下被禁的节点忽略点击
    // 输入: id —— 标签 id；输出: 无（变化时发 selectionChanged）
    function toggle(id) {
        var next = []
        if (tree.checkedIds.indexOf(id) >= 0) {
            for (var i = 0; i < tree.checkedIds.length; ++i) {
                if (tree.checkedIds[i] !== id)
                    next.push(tree.checkedIds[i])
            }
        } else {
            var disabled = tree._disabledSet()
            if (disabled[String(id)])
                return // 灰显：祖先已勾选，不可勾子节点
            var selfAncestors = tree._ancestorSet(id)
            for (var k = 0; k < tree.checkedIds.length; ++k) {
                var cid = tree.checkedIds[k]
                if (cid === id)
                    continue
                // 反链模式：被勾节点的祖先若已勾选则移出（即「打子节点时自动把祖先移出集合」）
                if (tree.enforceAntichain && selfAncestors[String(cid)])
                    continue
                next.push(cid)
            }
            next.push(id)
        }
        next = tree._sortByModel(next)
        tree.checkedIds = next
        tree.selectionChanged(next)
    }

    // 函数: 行点击（管理态=选中，打标/筛选态=勾选）
    // 输入: id —— 标签 id；输出: 无
    function activate(id) {
        if (tree.selectable) {
            tree.toggle(id)
        } else {
            tree.currentId = id
            tree.currentChanged(id)
        }
    }

    clip: true
    contentWidth: width
    contentHeight: rowHost.height
    boundsBehavior: Flickable.StopAtBounds

    Column {
        id: rowHost
        width: tree.width

        Repeater {
            model: tree.rows

            delegate: Rectangle {
                id: rowItem
                required property var modelData

                width: tree.width
                height: tree.rowHeight
                // 管理态选中行高亮
                color: (!tree.selectable && modelData.current) ? "#dbe6ff" : "transparent"

                // 整行点击（先声明，后续子元素的 MouseArea 覆盖其上）
                MouseArea {
                    anchors.fill: parent
                    onClicked: tree.activate(modelData.id)
                }

                // 行内容容器（普通 Item，非布局器；子项可用 anchors 垂直居中，
                // 且各子项 x 显式给定——避免在 Row 内使用 anchors 的不可靠行为）
                Item {
                    id: rowContent
                    anchors.verticalCenter: parent.verticalCenter
                    height: 18
                    x: 4 + modelData.depth * 14

                    // 折叠三角（有子节点才显示）
                    Text {
                        id: collapseMark
                        x: 0
                        width: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.hasChild ? (modelData.expanded ? "▾" : "▸") : ""
                        color: "#7a7a7a"
                        font.pixelSize: 10
                        MouseArea {
                            anchors.fill: parent
                            onClicked: tree.toggleCollapse(modelData.id)
                        }
                    }

                    // 复选框（仅打标/筛选态渲染）
                    Rectangle {
                        id: checkbox
                        x: 18
                        width: 14
                        height: 14
                        radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        visible: tree.selectable
                        color: modelData.checked ? "#86b2ff" : "transparent"
                        border.color: modelData.disabled ? "#cccccc" : "#8a94a6"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            visible: modelData.checked
                            text: "✓"
                            color: "#ffffff"
                            font.pixelSize: 10
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: tree.toggle(modelData.id)
                        }
                    }

                    // 标签名（复选框隐藏时左移，保持 6px 间距）
                    Text {
                        x: tree.selectable ? (checkbox.x + checkbox.width + 6) : (collapseMark.width + 6)
                        text: modelData.name
                        font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: (!tree.selectable && modelData.current) ? "#1b3a80"
                                 : (modelData.disabled ? "#aaaaaa"
                                 : (modelData.checked ? "#33619e" : "#2b2b2b"))
                    }
                }
            }
        }
    }
}
