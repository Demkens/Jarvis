# 侧边按钮栏
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

ColumnLayout {
    id: sideBar

    property int buttonSize: 35     // 按钮组件的固定尺寸
    property int iconSize: 20       // 按钮图标的固定尺寸
    property int barWidth: 45       // 侧边栏的优先宽度
    property int currentIndex: 0    // 当前选中的页面编号
    property var iconSources: [     // 按钮图标列表
        "qrc:/resource/icons/home.svg",
        "qrc:/resource/icons/file.svg",
        "qrc:/resource/icons/sun.svg",
        "qrc:/resource/icons/night.svg"
    ]

    Layout.preferredWidth: barWidth
    Layout.fillHeight: true

    clip: true

    // 顶部间距
    Item { Layout.preferredHeight: 5 }

    // 文件浏览按钮
    NormalButton {
        id: homeBtn
        Layout.alignment: Qt.AlignCenter
        iconSource: iconSources[0]
        selected: currentIndex === 0
        onClicked: currentIndex = 0
    }

    // 文件编辑按钮
    NormalButton {
        id: fileBtn
        Layout.alignment: Qt.AlignCenter
        iconSource: iconSources[1]
        selected: currentIndex === 1
        onClicked: currentIndex = 1
    }

    // 数据统计按钮
    NormalButton {
        id: dataBtn
        Layout.alignment: Qt.AlignCenter
        iconSource: iconSources[2]
        selected: currentIndex === 2
        onClicked: currentIndex = 2
    }

    // 文档撰写按钮
    NormalButton {
        id: docBtn
        Layout.alignment: Qt.AlignCenter
        iconSource: iconSources[3]
        selected: currentIndex === 3
        onClicked: currentIndex = 3
    }

    Item { Layout.fillHeight: true }

    // 按钮组件
    component NormalButton: Item {
        id: buttonRoot
        width: buttonSize; height: buttonSize

        property string iconSource: ""
        property bool selected: false

        signal clicked()

        // 按钮背景
        Rectangle {
            id: background
            anchors.fill: parent
            radius: 8
            color: buttonRoot.selected || mouseArea.containsMouse ? "#CFCFCF" : "transparent"

            // 禁用不必要的属性
            layer.enabled: false
        }

        // 按钮图标
        Image {
            id: icon
            anchors.centerIn: parent
            width: iconSize; height: iconSize
            source: buttonRoot.iconSource

            // 性能优化设置
            smooth: true
            mipmap: true
            antialiasing: true
            asynchronous: true
            cache: true
        }

        // 鼠标区域
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: buttonRoot.clicked()
        }
    }
}
