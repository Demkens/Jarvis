// 短笔记页（M8a）。
// 职责：维护当前库的笔记列表与编辑区，是笔记页交互状态的唯一持有者。
// 数据规则：正文以 Markdown 存于 envs/<库名>/note/<id>.md（由 noteService 读写），
//          库内 note 表只存标题与时间；文件是正文唯一真相。
// 交互规则：
// - 新建先进入内存草稿态，首次保存才落库，避免产生空记录；
// - 切换笔记/离开当前笔记前若有未保存修改，弹「保存 / 放弃 / 取消」；
// - 保存时若文件被外部修改/删除/出现，弹「覆盖 / 取消」，不静默覆盖；
// - 库切换时重载列表，未保存草稿丢弃并给出提示。
// 对外属性：无（直接使用 QML 上下文中的 libraryService / noteService）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    // 列表数据：[{id,title,createdTime,updatedTime,missing}]
    property var items: []
    // 当前打开的笔记 id（0=内存草稿，尚未落库）
    property int currentId: 0
    // 打开时文件的最后修改时间（毫秒，-1=文件缺失）；保存时作为外部改动基线
    property real currentMtime: -1
    // 当前笔记的正文文件是否缺失
    property bool currentMissing: false
    // 已落库内容基线：用于判断“改回原样”时是否仍算脏
    property string savedTitle: ""
    property string savedContent: ""
    // 是否有未保存修改
    property bool dirty: false
    // 程序化填充编辑器期间为 true，避免把赋值误判为用户编辑
    property bool loading: false
    // 提示/错误文本
    property string errorText: ""
    // 待执行的切换动作（switch/new）与目标笔记
    property string pendingAction: ""
    property int pendingNoteId: 0
    // 冲突确认后要回调的保存完成动作
    property var pendingSaveCallback: null
    // 库关闭时记录“草稿已丢弃”，等库重新打开后再提示，避免被二次刷新覆盖
    property bool draftDiscarded: false

    // 函数: 时间显示（ISO UTC → "YYYY-MM-DD HH:mm"）
    // 输入: iso —— 服务返回的时间字符串；输出: 展示文本（空值返回空串）
    function formatTime(iso) {
        var text = String(iso || "")
        if (text.length >= 16)
            return text.replace("T", " ").substring(0, 16)
        return text
    }

    // 函数: 把提示写入页面
    // 输入: result —— 服务返回的 {ok,message?}；输出: 无
    function showError(result) {
        page.errorText = (result && result.message) ? result.message : "操作失败"
        console.warn("[note] " + page.errorText)
    }

    // 函数: 以内存内容填充编辑器（不触发脏标记）
    // 输入: title/content/mtime/missing —— 笔记字段；输出: 无
    function applyEditor(title, content, mtime, missing) {
        page.loading = true
        titleField.text = title
        editor.text = content
        page.loading = false
        page.savedTitle = title
        page.savedContent = content
        page.currentMtime = mtime
        page.currentMissing = missing
        page.dirty = false
    }

    // 函数: 清空编辑器并回到“无选中”状态
    // 输入/输出: 无
    function clearEditor() {
        page.loading = true
        titleField.text = ""
        editor.text = ""
        page.loading = false
        page.currentId = 0
        page.savedTitle = ""
        page.savedContent = ""
        page.currentMtime = -1
        page.currentMissing = false
        page.dirty = false
    }

    // 函数: 重新加载列表
    // 输入: selectId —— 需要保持选中的笔记 id（0 表示不改选中）；输出: 是否成功
    function reloadList(selectId) {
        var result = noteService.listNotes()
        if (!result.ok) {
            page.showError(result)
            return false
        }
        // 深拷贝为纯 JS 数组，避免直接持有核心返回对象
        page.items = JSON.parse(JSON.stringify(result.items || []))
        if (selectId > 0)
            page.currentId = selectId
        return true
    }

    // 函数: 打开指定笔记
    // 输入: id —— 笔记 id；输出: 是否成功
    function loadNote(id) {
        var result = noteService.note(id)
        if (!result.ok) {
            page.showError(result)
            return false
        }
        var item = result.note
        page.currentId = item.id
        page.applyEditor(item.title, item.content, item.mtime, item.missing)
        page.errorText = ""
        return true
    }

    // 函数: 进入新建草稿态
    // 输入/输出: 无
    function startNew() {
        page.clearEditor()
        page.errorText = ""
        titleField.forceActiveFocus()
    }

    // 函数: 根据编辑器内容更新脏标记（内容改回原样则恢复为不脏）
    // 输入/输出: 无
    function updateDirty() {
        if (page.loading)
            return
        page.dirty = (titleField.text !== page.savedTitle || editor.text !== page.savedContent)
    }

    // 函数: 保存当前笔记（草稿则新建，已有则更新）
    // 输入: onSaved —— 保存成功后的回调（可空）；force —— true 表示用户已确认覆盖外部改动
    // 输出: 无（失败写入 errorText；冲突时弹出覆盖确认）
    function saveCurrent(onSaved, force) {
        if (page.currentId === 0) {
            var created = noteService.createNote(titleField.text, editor.text)
            if (!created.ok) {
                page.showError(created)
                return
            }
            page.currentId = created.id
            page.currentMtime = created.mtime
            page.currentMissing = false
            page.savedTitle = titleField.text
            page.savedContent = editor.text
            page.dirty = false
            page.errorText = ""
            page.reloadList(page.currentId)
            if (onSaved)
                onSaved()
            return
        }

        var saved = noteService.saveNote(page.currentId, titleField.text, editor.text,
                                         page.currentMtime, !!force)
        if (!saved.ok) {
            if (saved.conflict) {
                page.pendingSaveCallback = onSaved
                conflictDialog.message = saved.message || "笔记文件已被外部修改"
                conflictDialog.open()
                return
            }
            page.showError(saved)
            return
        }
        page.currentMtime = saved.mtime
        page.currentMissing = false
        page.savedTitle = titleField.text
        page.savedContent = editor.text
        page.dirty = false
        page.errorText = ""
        page.reloadList(page.currentId)
        if (onSaved)
            onSaved()
    }

    // 函数: 执行切换动作（保存/放弃后统一入口）
    // 输入/输出: 无
    function executePending() {
        var action = page.pendingAction
        page.pendingAction = ""
        if (action === "switch")
            page.loadNote(page.pendingNoteId)
        else if (action === "new")
            page.startNew()
    }

    // 函数: 请求切换到指定笔记（必要时先处理未保存修改）
    // 输入: id —— 目标笔记 id；输出: 无
    function requestSwitchNote(id) {
        if (id === page.currentId)
            return
        if (page.dirty) {
            page.pendingAction = "switch"
            page.pendingNoteId = id
            unsavedDialog.open()
            return
        }
        page.loadNote(id)
    }

    // 函数: 请求新建笔记（必要时先处理未保存修改）
    // 输入/输出: 无
    function requestNew() {
        if (page.dirty) {
            page.pendingAction = "new"
            page.pendingNoteId = 0
            unsavedDialog.open()
            return
        }
        page.startNew()
    }

    // 函数: 请求删除当前笔记（仅已落库的笔记可删）
    // 输入/输出: 无
    function requestDelete() {
        if (page.currentId <= 0)
            return
        deleteDialog.open()
    }

    // 函数: 执行删除，并在成功后选中列表首项
    // 输入/输出: 无
    function confirmDelete() {
        var result = noteService.deleteNote(page.currentId)
        if (!result.ok) {
            page.showError(result)
            return
        }
        page.clearEditor()
        page.errorText = ""
        page.reloadList(0)
        if (page.items.length > 0)
            page.loadNote(page.items[0].id)
    }

    // 库打开/切换/关闭：清空草稿，重载当前库列表并选中首项
    Connections {
        target: libraryService
        function onOpenChanged() {
            var droppedDraft = page.dirty
            unsavedDialog.close()
            deleteDialog.close()
            conflictDialog.close()
            page.pendingAction = ""
            page.pendingSaveCallback = null
            page.clearEditor()
            page.resetListPosition()

            // 关闭库：清空列表并记住草稿已丢弃，待重新打开时再提示
            if (!libraryService.isOpen) {
                page.items = []
                page.draftDiscarded = droppedDraft
                page.errorText = droppedDraft ? "库已关闭，未保存的笔记草稿已丢弃" : ""
                return
            }

            page.reloadList(0)
            var loaded = true
            if (page.items.length > 0)
                loaded = page.loadNote(page.items[0].id)
            if (page.draftDiscarded && loaded)
                page.errorText = "库已切换，未保存的笔记草稿已丢弃"
            else if (!page.draftDiscarded)
                page.errorText = ""
            page.draftDiscarded = false
        }
    }

    // 函数: 列表滚动回顶部（库切换后避免停留在越界位置）
    // 输入/输出: 无
    function resetListPosition() {
        noteList.positionViewAtBeginning()
    }

    Component.onCompleted: {
        page.reloadList(0)
        if (page.items.length > 0)
            page.loadNote(page.items[0].id)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 左栏：列表 ----
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: "#f7f9fc"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8

                    Text {
                        text: "短笔记"
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    NoteButton {
                        implicitWidth: 60
                        label: "新建"
                        baseColor: "#86b2ff"
                        textColor: "#ffffff"
                        onClicked: page.requestNew()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#e3e8f0"
                }

                ListView {
                    id: noteList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 6
                    clip: true
                    model: page.items
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 56
                        radius: 4
                        color: page.currentId === modelData.id ? "#e8f0ff" : "transparent"

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 3

                            Text {
                                width: parent.width
                                text: modelData.title !== "" ? modelData.title : "（无标题）"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                text: page.formatTime(modelData.updatedTime)
                                color: "#8a93a3"
                                font.pixelSize: 10
                            }
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8
                            visible: !!modelData.missing
                            text: "缺文件"
                            color: "#c0392b"
                            font.pixelSize: 10
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.requestSwitchNote(modelData.id)
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: page.items.length === 0
                        text: "暂无笔记，点击「新建」开始"
                        color: "#9aa3b2"
                        font.pixelSize: 11
                    }
                }
            }
        }

        // ---- 右栏：编辑区 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    NoteButton {
                        label: "删除"
                        baseColor: "#e6eaf2"
                        textColor: "#c0392b"
                        enabled: page.currentId > 0
                        onClicked: page.requestDelete()
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: page.currentId === 0
                              ? (page.dirty ? "未保存" : "新笔记")
                              : (page.dirty ? "未保存" : "已保存")
                        color: page.dirty ? "#c0752a" : "#7a7a7a"
                        font.pixelSize: 11
                    }

                    NoteButton {
                        label: "保存"
                        baseColor: "#86b2ff"
                        textColor: "#ffffff"
                        enabled: page.dirty
                                 && (titleField.text.trim() !== "" || editor.text.trim() !== "")
                        onClicked: page.saveCurrent(null, false)
                    }
                }

                TextField {
                    id: titleField
                    Layout.fillWidth: true
                    placeholderText: "标题（可空）"
                    font.pixelSize: 15
                    selectByMouse: true
                    onTextChanged: page.updateDirty()
                }

                // 文件缺失提示（保存后会重建文件）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    visible: page.currentId > 0 && page.currentMissing
                    color: "#fdf1ef"
                    border.color: "#e6b3ad"
                    border.width: 1
                    radius: 4

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.currentId > 0 && page.currentMissing
                              ? "笔记文件缺失，保存时将重新创建"
                              : ""
                        color: "#c0392b"
                        font.pixelSize: 11
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        id: editor
                        placeholderText: "在此输入 Markdown 正文…"
                        font.pixelSize: 13
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        onTextChanged: page.updateDirty()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: page.errorText !== ""
                    text: page.errorText
                    color: "#c0392b"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    // ---- 未保存修改确认 ----
    Popup {
        id: unsavedDialog
        width: 380
        padding: 16
        modal: true
        focus: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "当前笔记有未保存的修改"
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "保存后再继续，或放弃本次修改。"
                color: "#7a7a7a"
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }
                NoteButton {
                    label: "取消"
                    onClicked: {
                        page.pendingAction = ""
                        unsavedDialog.close()
                    }
                }
                NoteButton {
                    label: "放弃"
                    baseColor: "#e6eaf2"
                    textColor: "#c0392b"
                    onClicked: {
                        page.dirty = false
                        unsavedDialog.close()
                        page.executePending()
                    }
                }
                NoteButton {
                    label: "保存"
                    baseColor: "#86b2ff"
                    textColor: "#ffffff"
                    enabled: page.dirty
                             && (titleField.text.trim() !== "" || editor.text.trim() !== "")
                    onClicked: {
                        unsavedDialog.close()
                        page.saveCurrent(function () { page.executePending() }, false)
                    }
                }
            }
        }
    }

    // ---- 删除确认 ----
    Popup {
        id: deleteDialog
        width: 360
        padding: 16
        modal: true
        focus: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "删除笔记"
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "将删除「"
                      + (titleField.text.trim() !== "" ? titleField.text.trim() : "无标题笔记")
                      + "」及其 Markdown 文件，且不可恢复。"
                color: "#c0392b"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }
                NoteButton {
                    label: "取消"
                    onClicked: deleteDialog.close()
                }
                NoteButton {
                    label: "确认删除"
                    implicitWidth: 84
                    baseColor: "#c0392b"
                    textColor: "#ffffff"
                    onClicked: {
                        deleteDialog.close()
                        page.confirmDelete()
                    }
                }
            }
        }
    }

    // ---- 外部改动覆盖确认 ----
    Popup {
        id: conflictDialog
        property string message: ""
        width: 400
        padding: 16
        modal: true
        focus: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "文件已被外部改动"
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: conflictDialog.message
                color: "#7a7a7a"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: "选择「覆盖」将用编辑器内容替换文件；选择「取消」保留外部版本。"
                color: "#c0752a"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }
                NoteButton {
                    label: "取消"
                    onClicked: {
                        page.pendingSaveCallback = null
                        conflictDialog.close()
                    }
                }
                NoteButton {
                    label: "覆盖"
                    baseColor: "#c0752a"
                    textColor: "#ffffff"
                    onClicked: {
                        var callback = page.pendingSaveCallback
                        page.pendingSaveCallback = null
                        conflictDialog.close()
                        page.saveCurrent(callback, true)
                    }
                }
            }
        }
    }

    // 页内统一按钮（保留项目既有的 Rectangle + Text + MouseArea 形态）
    component NoteButton: Rectangle {
        id: button

        property string label: ""
        property color baseColor: "#e6eaf2"
        property color textColor: "#2b2b2b"
        signal clicked()

        implicitWidth: 72
        implicitHeight: 30
        radius: 4
        color: button.baseColor
        opacity: button.enabled ? 1.0 : 0.5

        Text {
            anchors.centerIn: parent
            text: button.label
            color: button.textColor
            font.pixelSize: 12
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }
}
