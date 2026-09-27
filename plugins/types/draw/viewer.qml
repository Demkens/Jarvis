// draw 类型包 · 图集查看器（开发文档 7.4）
// 由核心 ViewerWindow 通过 Loader 嵌入并注入：
//   property var packageView  PackageViewContext：
//     files（按序号的 [{index,name,url,size}]）、positionJson、savePosition()、title
// 行为：按序号顺序浏览；翻页/缩放/跳页；位置实时写回（{"index": n}，1 起）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: viewer

    // 核心注入
    property var packageView: null
    property int pageIndex: 0    // 当前页（0 基）
    property real zoom: 1.0

    function pageCount() {
        return packageView ? packageView.files.length : 0
    }

    function gotoPage(index) {
        var n = Math.max(0, Math.min(index, pageCount() - 1))
        if (n === viewer.pageIndex)
            return
        viewer.pageIndex = n
        positionTimer.restart()
    }

    // 打开时从浏览记忆续看（{"index": 3} → 第 3 张，索引 2）。
    // 注意：packageView 由外层 Loader.onLoaded 注入，晚于本组件 onCompleted 触发，
    // 因此用 onPackageViewChanged 恢复位置，而不是 Component.onCompleted。
    onPackageViewChanged: {
        if (packageView && packageView.positionJson.length > 0) {
            try {
                var pos = JSON.parse(packageView.positionJson)
                if (pos.index !== undefined)
                    // 上界 clamp：包文件被外部删减后记忆索引不得越界
                    viewer.pageIndex = Math.max(0, Math.min(pos.index - 1, pageCount() - 1))
            } catch (e) { /* 损坏的位置忽略，从头开始 */ }
        }
    }

    // 位置变化后防抖写回（浏览中实时记忆，不触发 PackageUpdated）
    Timer {
        id: positionTimer
        interval: 400
        repeat: false
        onTriggered: {
            if (!packageView)
                return
            packageView.positionJson = JSON.stringify({index: viewer.pageIndex + 1})
            packageView.savePosition()
        }
    }

    focus: true
    Keys.onLeftPressed: prevButton.click()
    Keys.onRightPressed: nextButton.click()
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
            zoomSlider.value = Math.min(zoomSlider.to, zoomSlider.value + 0.1)
        else if (event.key === Qt.Key_Minus)
            zoomSlider.value = Math.max(zoomSlider.from, zoomSlider.value - 0.1)
        else if (event.key === Qt.Key_0)
            fitZoom()
    }

    function fitZoom() {
        viewer.zoom = 1.0
        zoomSlider.value = 1.0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // 工具条
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 6
            spacing: 10

            Label {
                text: packageView ? packageView.title : ""
                font.pixelSize: 14
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Button { id: zoomOutBtn; text: "－"; onClicked: zoomSlider.value -= 0.1 }
            Slider {
                id: zoomSlider
                from: 0.2; to: 4.0; value: 1.0
                stepSize: 0.05
                Layout.preferredWidth: 160
                onValueChanged: viewer.zoom = value
            }
            Button { text: "＋"; onClicked: zoomSlider.value += 0.1 }
            Button { text: "适宽"; onClicked: viewer.fitZoom() }
        }

        // 画布
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Image {
                id: canvas
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: pageCount() > 0
                        ? packageView.files[viewer.pageIndex].url : ""
                width: implicitWidth * viewer.zoom
                height: implicitHeight * viewer.zoom
                smooth: true
                cache: false

                Text {
                    visible: canvas.status === Image.Loading
                    anchors.centerIn: parent
                    text: "加载中…"
                    color: "#888888"
                }
                Text {
                    visible: canvas.status === Image.Error && pageCount() > 0
                    anchors.centerIn: parent
                    text: "图片读取失败（实体库可能已离线）"
                    color: "#c0341d"
                }
            }

            Text {
                visible: pageCount() === 0
                anchors.centerIn: parent
                text: "包内没有可显示的图片"
                color: "#888888"
            }

            // 滚轮翻页
            WheelHandler {
                target: null
                onWheel: function (event) {
                    if (event.angleDelta.y > 0)
                        prevButton.click()
                    else if (event.angleDelta.y < 0)
                        nextButton.click()
                }
            }
        }

        // 翻页栏
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            spacing: 12

            Button {
                id: prevButton
                text: "上一张"
                enabled: viewer.pageIndex > 0
                onClicked: viewer.gotoPage(viewer.pageIndex - 1)
            }
            Label {
                font.pixelSize: 13
                text: pageCount() > 0
                      ? (viewer.pageIndex + 1) + " / " + pageCount() : "0 / 0"
            }
            SpinBox {
                from: 1; to: Math.max(1, pageCount())
                value: viewer.pageIndex + 1
                enabled: pageCount() > 1
                onValueModified: viewer.gotoPage(value - 1)
                editable: true
            }
            Button {
                id: nextButton
                text: "下一张"
                enabled: viewer.pageIndex < pageCount() - 1
                onClicked: viewer.gotoPage(viewer.pageIndex + 1)
            }
        }
    }
}
