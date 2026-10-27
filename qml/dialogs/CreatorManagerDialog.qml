// 创作者管理弹窗（M11a）：顶栏「工具 → 创作者管理…」唤起。
// 职责：列出创作者及其别名/关联包数；重命名正式名；增删别名。
// 数据规则：
// - 分面名与别名都是导入时的匹配键，别名在全库内必须唯一（界面层收紧，数据库仍保留重复能力）；
// - 「佚名」是系统保留创作者（id=0）：不可重命名，也不接纳别名；
// - 所有写操作经 TypeEngine 门面，成功后本弹窗自行重载列表。
// 对外方法：openManager() —— 复位搜索与选中状态后弹出。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: dialog

    // 创作者列表：[{id,name,packageCount,aliases[],system}]
    property var items: []
    // 当前选中的创作者 id（0=未选）
    property int currentId: 0
    // 当前选中创作者的展示字段
    property string currentName: ""
    property bool currentSystem: false
    property int currentPackageCount: 0
    property var currentAliases: []
    // 提示文本（空=无提示）
    property string errorText: ""

    width: 700
    height: 580
    modal: true
    focus: true
    padding: 16
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    // 函数: 在已加载列表里按 id 查找条目
    // 输入: id —— 创作者 id；输出: 条目对象（找不到返回 null）
    function itemById(id) {
        for (var i = 0; i < dialog.items.length; ++i) {
            if (dialog.items[i].id === id)
                return dialog.items[i]
        }
        return null
    }

    // 函数: 让右侧面板与选中项同步；未选中时清空字段
    // 输入: id —— 创作者 id；输出: 无
    function selectCreator(id) {
        var item = dialog.itemById(id)
        if (item === null) {
            dialog.currentId = 0
            dialog.currentName = ""
            dialog.currentSystem = false
            dialog.currentPackageCount = 0
            dialog.currentAliases = []
            renameField.text = ""
            aliasField.text = ""
            return
        }
        dialog.currentId = item.id
        dialog.currentName = item.name
        dialog.currentSystem = !!item.system
        dialog.currentPackageCount = Number(item.packageCount || 0)
        // 深拷贝为纯 JS 数组，避免直接持有核心返回对象
        dialog.currentAliases = JSON.parse(JSON.stringify(item.aliases || []))
        renameField.text = item.name
        aliasField.text = ""
    }

    // 函数: 重新加载列表并按 keepId 保持选中（找不到则选首项）
    // 输入: keepId —— 期望保持选中的创作者 id（0=选首项）；输出: 无
    function loadCreators(keepId) {
        var result = typeEngine.listCreators(searchField.text)
        if (!result || !result.ok) {
            dialog.items = []
            dialog.selectCreator(0)
            dialog.errorText = (result && result.message) ? result.message : "读取创作者失败"
            return
        }
        dialog.items = JSON.parse(JSON.stringify(result.items || []))
        if (dialog.items.length === 0) {
            dialog.selectCreator(0)
            return
        }
        var target = (keepId > 0 && dialog.itemById(keepId) !== null) ? keepId : dialog.items[0].id
        dialog.selectCreator(target)
    }

    // 函数: 复位状态并弹出
    // 输入/输出: 无
    function openManager() {
        dialog.errorText = ""
        searchField.text = ""
        aliasField.text = ""
        dialog.loadCreators(0)
        open()
    }

    // 函数: 重命名当前创作者
    // 输入/输出: 无
    function renameCurrent() {
        if (dialog.currentId <= 0)
            return
        var name = renameField.text.trim()
        if (name === "") {
            dialog.errorText = "请输入新的正式名"
            return
        }
        var result = typeEngine.renameCreator(dialog.currentId, name)
        if (!result || !result.ok) {
            dialog.errorText = (result && result.message) ? result.message : "重命名失败"
            return
        }
        dialog.errorText = ""
        dialog.loadCreators(dialog.currentId)
    }

    // 函数: 为当前创作者添加别名
    // 输入/输出: 无
    function addAlias() {
        if (dialog.currentId <= 0 || dialog.currentSystem)
            return
        var alias = aliasField.text.trim()
        if (alias === "") {
            dialog.errorText = "请输入别名"
            return
        }
        var result = typeEngine.addCreatorAlias(dialog.currentId, alias)
        if (!result || !result.ok) {
            dialog.errorText = (result && result.message) ? result.message : "添加别名失败"
            return
        }
        dialog.errorText = ""
        aliasField.text = ""
        dialog.loadCreators(dialog.currentId)
    }

    // 函数: 移除当前创作者的一条别名
    // 输入: alias —— 别名文本；输出: 无
    function removeAlias(alias) {
        if (dialog.currentId <= 0 || dialog.currentSystem)
            return
        var result = typeEngine.removeCreatorAlias(dialog.currentId, alias)
        if (!result || !result.ok) {
            dialog.errorText = (result && result.message) ? result.message : "移除别名失败"
            return
        }
        dialog.errorText = ""
        dialog.loadCreators(dialog.currentId)
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "创作者管理"
            font.pixelSize: 15
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "别名用于导入时把不同写法归到同一创作者。别名在全库内唯一；「佚名」为系统默认创作者，不可重命名。"
            color: "#7a7a7a"
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ---- 左：搜索 + 创作者列表 ----
            ColumnLayout {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                spacing: 6

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: "搜索正式名或别名"
                    font.pixelSize: 12
                    selectByMouse: true
                    onTextChanged: dialog.loadCreators(dialog.currentId)
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#f7f9fc"
                    border.color: "#d5dbe6"
                    border.width: 1
                    radius: 4

                    ListView {
                        id: creatorList
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: dialog.items
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 48
                            radius: 4
                            color: dialog.currentId === modelData.id ? "#e8f0ff" : "transparent"

                            Column {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 2

                                RowLayout {
                                    width: parent.width
                                    spacing: 6

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: !!modelData.system
                                        text: "系统"
                                        color: "#8a93a3"
                                        font.pixelSize: 10
                                    }
                                }
                                Text {
                                    text: Number(modelData.packageCount || 0) + " 个包 · "
                                          + (modelData.aliases || []).length + " 个别名"
                                    color: "#8a93a3"
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    dialog.errorText = ""
                                    dialog.selectCreator(modelData.id)
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: dialog.items.length === 0
                            text: searchField.text.trim() === "" ? "暂无创作者" : "没有匹配的创作者"
                            color: "#9aa3b2"
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // ---- 右：选中创作者的正式名与别名 ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    visible: dialog.currentId <= 0
                    text: "请选择左侧的创作者"
                    color: "#9aa3b2"
                    font.pixelSize: 12
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: dialog.currentId > 0

                    Text {
                        Layout.fillWidth: true
                        text: dialog.currentName
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "关联 " + dialog.currentPackageCount + " 个数据包"
                        color: "#7a7a7a"
                        font.pixelSize: 11
                    }

                    // 正式名重命名
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: renameField
                            Layout.fillWidth: true
                            placeholderText: "正式名"
                            font.pixelSize: 12
                            selectByMouse: true
                            enabled: !dialog.currentSystem
                        }
                        DialogButton {
                            label: "重命名"
                            enabled: !dialog.currentSystem
                            onClicked: dialog.renameCurrent()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: dialog.currentSystem
                        text: "系统保留创作者，不能重命名，也不接纳别名。"
                        color: "#9aa3b2"
                        font.pixelSize: 10
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#e3e8f0"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "别名"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: aliasField
                            Layout.fillWidth: true
                            placeholderText: "新增别名（如作者的其他署名）"
                            font.pixelSize: 12
                            selectByMouse: true
                            enabled: !dialog.currentSystem
                            onAccepted: dialog.addAlias()
                        }
                        DialogButton {
                            label: "添加"
                            enabled: !dialog.currentSystem
                            onClicked: dialog.addAlias()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#f7f9fc"
                        border.color: "#d5dbe6"
                        border.width: 1
                        radius: 4

                        ListView {
                            id: aliasList
                            anchors.fill: parent
                            anchors.margins: 4
                            clip: true
                            model: dialog.currentAliases
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {}

                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: 32
                                radius: 3
                                color: "transparent"

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: removeButton.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    text: modelData
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    id: removeButton
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 44
                                    height: 22
                                    radius: 3
                                    color: removeMouse.containsMouse ? "#f4d9d5" : "#e6eaf2"
                                    visible: !dialog.currentSystem

                                    Text {
                                        anchors.centerIn: parent
                                        text: "移除"
                                        color: "#c0392b"
                                        font.pixelSize: 10
                                    }
                                    MouseArea {
                                        id: removeMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: dialog.removeAlias(modelData)
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: dialog.currentAliases.length === 0
                                text: dialog.currentSystem ? "系统创作者无别名" : "暂无别名"
                                color: "#9aa3b2"
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.errorText !== ""
                    text: dialog.errorText
                    color: "#c0392b"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item { Layout.fillWidth: true }
                    DialogButton {
                        label: "关闭"
                        onClicked: dialog.close()
                    }
                }
            }
        }
    }

    // 弹窗内统一按钮（沿用项目的 Rectangle + Text + MouseArea 形态）
    component DialogButton: Rectangle {
        id: button

        property string label: ""
        signal clicked()

        implicitWidth: 72
        implicitHeight: 30
        radius: 4
        color: if (!button.enabled) {
                   return "#eef1f6"
               } else if (buttonMouse.containsMouse) {
                   return "#d8e3f7"
               } else {
                   return "#e6eaf2"
               }
        opacity: button.enabled ? 1.0 : 0.6

        Text {
            anchors.centerIn: parent
            text: button.label
            color: "#2b2b2b"
            font.pixelSize: 12
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: button.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }
}
