import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: root
    width: 500; height: 250
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true

    ColumnLayout {
        spacing: 15
        anchors.fill: parent

        // 库名称输入
        Label { text: "数据库名称:" }
        TextField {
            id: dbNameField
            Layout.fillWidth: true
            placeholderText: "待输入..."
        }

        // 目录选择
        Label { text: "链接库地址:" }
        RowLayout {
            TextField {
                id: directoryField
                Layout.fillWidth: true
                placeholderText: "待选择..."
                readOnly: true
            }
            Button {
                text: "浏览..."
                onClicked: folderDialog.open()
            }
        }

        // 自定义操作按钮
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            Button {
                text: "创建"
                highlighted: true
                onClicked: createDb()
            }
            Button {
                text: "取消"
                flat: true
                onClicked: root.close()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "选择保存目录"
        onAccepted: {
            // 处理路径格式（移除file://前缀）
            var path = selectedFolder.toString();
            if (path.startsWith("file://")) {
                path = path.substring(7);
            }
            // Windows平台特殊处理
            if (Qt.platform.os === "windows") {
                if (path.startsWith("/")) {
                    path = path.substring(1);
                }
                path = path.replace(/\//g, "\\");
            }
            directoryField.text = path;
        }
    }

    MessageDialog {
        id: errorDialog
        title: "数据库创建错误"
        buttons: MessageDialog.Ok
    }

    function createDb() {
        // 验证输入
        if (dbNameField.text.trim() === "")
        {
            errorDialog.text = "数据库名称不能为空";
            errorDialog.open();
            return;
        }

        if (directoryField.text.trim() === "")
        {
            errorDialog.text = "请选择保存目录";
            errorDialog.open();
            return;
        }

        // 输入有效时创建数据库
        if (parent.dbModelalies.createDatabase(directoryField.text.trim(), dbNameField.text.trim()))
        {
            console.log("数据库创建成功");
            root.close();
        }
        else
        {
            errorDialog.text = "数据库创建失败";
            errorDialog.open();
        }
    }

    onClosed: parent.source = ""
}
