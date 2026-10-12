// 通用图标按钮(窗口控制/设置等顶栏按钮)；悬停提亮
// 外层必须使用 Layout 管理
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

Image {
    id: winBtn

    // 输入: 图标资源路径（qrc 绝对路径）
    property string iconSource: ""

    // 输出: 鼠标点击后触发，由调用方绑定具体窗口行为
    signal clicked()

    Layout.preferredWidth: 30; Layout.preferredHeight: 30
    sourceSize.width: 128; sourceSize.height: 128
    fillMode: Image.PreserveAspectFit
    source: winBtn.iconSource

    Layout.alignment: Qt.AlignVCenter

    // 悬停提亮
    layer.enabled: false
    layer.effect: MultiEffect {
        colorizationColor: "white"
        colorization: 1.0
        brightness: 0.5
    }

    MouseArea {
        hoverEnabled: true

        anchors.fill: parent

        onEntered: winBtn.layer.enabled = true
        onExited: winBtn.layer.enabled = false
        onClicked: winBtn.clicked()
    }
}
