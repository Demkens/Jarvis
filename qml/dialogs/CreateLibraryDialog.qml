// 建库弹窗（开发文档 8.6）：库名、链接库目录、勾选已安装类型
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dlg
    title: "新建库"
    modal: true
    anchors.centerIn: parent ? parent : null
    width: 520
    standardButtons: Dialog.NoButton

    signal completed(string name)

    // 由 Main.qml 注入
    property var libraryService: null
    property var typePackageManager: null

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "库名："; Layout.preferredWidth: 90 }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "如 Main；即 envs 下的数据集目录名"
                selectByMouse: true
                onAccepted: createButton.click()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "链接库："; Layout.preferredWidth: 90 }
            TextField {
                id: linkField
                Layout.fillWidth: true
                readOnly: true
                placeholderText: "实体数据包存放目录（本机硬盘/NAS）"
            }
            Button {
                text: "浏览…"
                onClicked: folderPicker.open()
            }
        }

        Label {
            text: "启用类型："
            leftPadding: 0
        }
        Repeater {
            id: typesRepeater
            model: dlg.typePackageManager ? dlg.typePackageManager.loadedPackages : []
            delegate: CheckBox {
                required property var modelData
                text: modelData.displayName + "（" + modelData.form + "）"
                property string form: modelData.form
                checked: true // M1 只有 draw，默认勾选
                leftPadding: 20
            }
        }

        Text {
            id: errorText
            Layout.fillWidth: true
            color: "#c0341d"
            wrapMode: Text.WordWrap
            visible: text !== ""
            text: ""
        }
    }

    footer: DialogButtonBox {
        Button {
            text: "取消"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            id: createButton
            text: "创建"
            highlighted: true
            // 不能用 AcceptRole：它会在点击时无条件自动关闭弹窗，
            // 导致核心校验失败（如链接库目录为空）时错误信息来不及显示。
            // ActionRole 不自动关窗，仅在创建成功后手动 accept()。
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                // 临时诊断日志（问题确认后移除）
                console.log("[create] clicked. service =", dlg.libraryService,
                            "name =", JSON.stringify(nameField.text),
                            "link =", JSON.stringify(linkField.text))
                const forms = []
                for (let i = 0; i < typesRepeater.count; ++i) {
                    const box = typesRepeater.itemAt(i)
                    if (box.checked && forms.indexOf(box.form) === -1)
                        forms.push(box.form)
                }
                const result = dlg.libraryService.createLibrary(
                    nameField.text, linkField.text, forms)
                console.log("[create] result =", JSON.stringify(result))
                if (result && result.ok) {
                    errorText.text = ""
                    dlg.completed(result.name)
                    dlg.accept()
                } else {
                    // 失败：弹窗保持打开，红字告知原因
                    errorText.text = result ? result.message
                                            : "创建调用未返回结果（服务未注入）"
                }
            }
        }
    }

    FolderDialog {
        id: folderPicker
        title: "选择链接库目录"
        onAccepted: {
            // selectedFolder 为 file:/// URL，转成本机路径（存储时核心再统一为 '/' 风格）
            linkField.text = decodeURIComponent(
                selectedFolder.toString().replace(/^file:\/\/\//, ""))
        }
    }

    onOpened: {
        nameField.text = ""
        linkField.text = ""
        errorText.text = ""
        for (let i = 0; i < typesRepeater.count; ++i)
            typesRepeater.itemAt(i).checked = true
    }
}
