// 窗口控制按钮组(最小化/最大化/关闭)
import QtQuick
import QtQuick.Layouts

import "."

RowLayout {
    id: winControls
    spacing: 0

    WindowButton {
        iconSource: "qrc:/img/Resources/title/min.png"
        onClicked: winControls.Window.window.showMinimized()
    }
    WindowButton {
        iconSource: winControls.Window.window.visibility === Window.Maximized ? "qrc:/img/Resources/title/mini.png" : "qrc:/img/Resources/title/max.png"
        onClicked: {
            const w = winControls.Window.window
            if (w.visibility === Window.Maximized) w.showNormal()
            else w.showMaximized()
        }
    }
    WindowButton {
        iconSource: "qrc:/img/Resources/title/close.png"
        onClicked: Qt.quit()
    }
}
