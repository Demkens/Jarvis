// 混合媒体类型包（photo）· 查看器（公共实现）
// 职责：把包内图片与视频按包内序号统一成一条浏览序列，负责三件事——
//   ① 逐项浏览：上一项/下一项、序号跳转、滚轮翻页；图片项适应窗口显示并支持缩放拖动；
//   ② 视频项：内嵌 VideoPlayer，翻到即停在首帧，由用户点击播放（不自动播放、不自动翻页）；
//   ③ 续看：{"index": 项号(1 起), "positionMs": 毫秒}，图片项 positionMs 恒为 0，
//           视频项记录播放进度；播放中每 10 秒、暂停、翻项与关窗时写回。
// 边界：只消费 packageView 的 files/positionJson/savePosition，不碰数据库与文件系统；
//       解码与播放控件全部复用 VideoPlayer，本文件只做"选当前项"与记忆读写。
// 由核心 ViewerWindow 通过 Loader 嵌入并注入：
//   property var packageView —— PackageViewContext：
//     files（[{index,name,relativePath,size,url,media}]）、positionJson、savePosition()、title
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "."

Item {
    id: viewer

    // 核心注入
    property var packageView: null

    // 包内可浏览项（按包内序号排序；只保留图片与视频，忽略其它文件）
    readonly property var items: {
        var out = []
        if (!packageView)
            return out
        for (var i = 0; i < packageView.files.length; ++i) {
            var f = packageView.files[i]
            if (f.media === "image" || f.media === "video")
                out.push(f)
        }
        return out
    }

    // 当前项序号（0 基；界面与记忆统一用 +1）
    property int pageIndex: 0

    // 图片缩放倍率（1.0 = 适应窗口；仅图片项生效）
    property real zoom: 1.0

    // 视频项起播位置（毫秒）：仅在"从记忆恢复的当前项恰好是视频"时非 0
    property int startPositionMs: 0

    readonly property var currentItem: (items.length > 0
                                        && pageIndex >= 0 && pageIndex < items.length)
                                       ? items[pageIndex] : null
    readonly property bool currentIsVideo: currentItem !== null
                                           && currentItem.media === "video"

    // 函数: 项数
    // 输入/输出: 无；输出: 可浏览项总数
    function itemCount() {
        return viewer.items.length
    }

    // 恢复续看位置：{"index": 项号(1 起), "positionMs": 毫秒}。
    // packageView 由外层 Loader.onLoaded 注入，晚于本组件 onCompleted，故用 onPackageViewChanged
    onPackageViewChanged: {
        var restoredIndex = 0
        var restoredPosition = 0
        if (packageView && packageView.positionJson.length > 0) {
            try {
                var pos = JSON.parse(packageView.positionJson)
                if (pos.index !== undefined)
                    // 上界 clamp：包文件被外部删减后记忆项号不得越界
                    restoredIndex = Math.max(0, Math.min(pos.index - 1, viewer.items.length - 1))
                if (pos.positionMs !== undefined)
                    restoredPosition = Math.max(0, pos.positionMs)
            } catch (e) { /* 损坏的位置忽略，从第一项开始 */ }
        }
        viewer.startPositionMs = restoredPosition
        viewer.pageIndex = restoredIndex
    }

    // 函数: 立即把当前项（及视频进度）写入浏览记忆
    // 输入/输出: 无；播放定时、暂停、翻项、关窗时调用
    function savePositionNow() {
        if (!packageView)
            return
        var ms = 0
        if (viewer.currentIsVideo && videoLoader.item !== null)
            ms = videoLoader.item.positionMs
        packageView.positionJson = JSON.stringify({
            index: viewer.pageIndex + 1,
            positionMs: ms
        })
        packageView.savePosition()
    }

    // 函数: 跳到指定项
    // 输入: index —— 目标项（0 基，越界自动钳制）；输出: 无
    // 说明：先把旧项（若为视频）的进度写回，再切换；新项一律从头开始
    function gotoPage(index) {
        var n = Math.max(0, Math.min(index, viewer.items.length - 1))
        if (n === viewer.pageIndex)
            return
        viewer.savePositionNow()
        viewer.startPositionMs = 0
        viewer.zoom = 1.0
        viewer.pageIndex = n
        positionTimer.restart()
    }

    // 函数: 缩放复位到适应窗口
    // 输入/输出: 无
    function fitZoom() {
        viewer.zoom = 1.0
        zoomSlider.value = 1.0
    }

    // 翻项后的防抖写回（浏览中实时记忆）
    Timer {
        id: positionTimer
        interval: 400
        repeat: false
        onTriggered: viewer.savePositionNow()
    }

    // 视频播放中每 10 秒写回一次进度（暂停/关窗由事件立即写回）
    Timer {
        interval: 10000
        repeat: true
        running: videoLoader.item !== null && videoLoader.item.playing
        onTriggered: viewer.savePositionNow()
    }

    // 图片项使用：左右翻项、+/- 缩放、0 复位；
    // 视频项由 VideoPlayer 抢焦点，方向键改为快退/快进（各自语义明确）
    focus: true
    Keys.onLeftPressed: prevButton.click()
    Keys.onRightPressed: nextButton.click()
    Keys.onPressed: function (event) {
        if (!viewer.currentIsVideo && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal))
            zoomSlider.value = Math.min(zoomSlider.to, zoomSlider.value + 0.1)
        else if (!viewer.currentIsVideo && event.key === Qt.Key_Minus)
            zoomSlider.value = Math.max(zoomSlider.from, zoomSlider.value - 0.1)
        else if (!viewer.currentIsVideo && event.key === Qt.Key_0)
            viewer.fitZoom()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // ===== 工具条：标题 + 类型标记 + 缩放（仅图片项） =====
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
                elide: Label.ElideRight
                Layout.maximumWidth: 360
            }
            Label {
                visible: viewer.currentItem !== null
                text: viewer.currentIsVideo ? "视频" : "图片"
                font.pixelSize: 11
                color: "#5c6472"
            }
            Item { Layout.fillWidth: true }

            Button {
                text: "－"
                visible: !viewer.currentIsVideo
                onClicked: zoomSlider.value -= 0.1
            }
            Slider {
                id: zoomSlider
                visible: !viewer.currentIsVideo
                from: 0.2; to: 4.0; value: 1.0
                stepSize: 0.05
                Layout.preferredWidth: 160
                onValueChanged: viewer.zoom = value
            }
            Button {
                text: "＋"
                visible: !viewer.currentIsVideo
                onClicked: zoomSlider.value += 0.1
            }
            Button {
                text: "适应窗口"
                visible: !viewer.currentIsVideo
                onClicked: viewer.fitZoom()
            }
        }

        // ===== 内容区：图片画布 / 视频播放器（同一位置互斥显示） =====
        Item {
            id: canvasArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // 图片项：默认适应窗口（完整显示），放大后可拖动
            Item {
                id: imageArea
                anchors.fill: parent
                visible: !viewer.currentIsVideo

                Flickable {
                    id: flick
                    anchors.fill: parent
                    contentWidth: Math.max(width, imageItem.width)
                    contentHeight: Math.max(height, imageItem.height)
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: imageItem.width > width || imageItem.height > height

                    Image {
                        id: imageItem
                        x: Math.max(0, (flick.width - width) / 2)
                        y: Math.max(0, (flick.height - height) / 2)
                        // 缩放基准：zoom=1.0 时等比缩放到刚好完整放入视口
                        readonly property real fitScale:
                            (implicitWidth > 0 && implicitHeight > 0
                             && flick.width > 0 && flick.height > 0)
                            ? Math.min(flick.width / implicitWidth, flick.height / implicitHeight)
                            : 1.0
                        width: implicitWidth * fitScale * viewer.zoom
                        height: implicitHeight * fitScale * viewer.zoom
                        source: (viewer.currentItem !== null && !viewer.currentIsVideo)
                                ? viewer.currentItem.url : ""
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        cache: false
                    }

                    // 滚轮翻项（消费 wheel 事件，避免 Flickable 同时滚动内容）
                    WheelHandler {
                        onWheel: function (event) {
                            event.accepted = true
                            if (event.angleDelta.y > 0)
                                prevButton.click()
                            else if (event.angleDelta.y < 0)
                                nextButton.click()
                        }
                    }
                }

                Text {
                    visible: imageItem.status === Image.Loading && viewer.currentItem !== null
                    anchors.centerIn: parent
                    text: "加载中…"
                }
                Text {
                    visible: imageItem.status === Image.Error && viewer.currentItem !== null
                    anchors.centerIn: parent
                    text: "图片读取失败（实体库可能已离线）"
                    color: "#c0341d"
                }
                Text {
                    visible: viewer.itemCount() === 0
                    anchors.centerIn: parent
                    text: "包内没有可浏览的图片或视频"
                }
            }

            // 视频项：按需创建播放器（图片项不创建，避免无谓的解码资源占用）；
            // 创建后把焦点交给播放器，保证空格/方向键/F 立即生效
            Loader {
                id: videoLoader
                anchors.fill: parent
                active: viewer.currentIsVideo
                sourceComponent: videoComponent
                onLoaded: if (item) item.forceActiveFocus()
            }
        }

        // 视频项提示（图片项的快捷键已由 ImageSequenceViewer 的用户习惯覆盖，无需重复提示）
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            visible: viewer.currentIsVideo
            font.pixelSize: 11
            color: "#5c6472"
            text: "点击画面或按空格 播放/暂停 · ←→ 快退/快进 5 秒 · ↑↓ 音量 · F 全屏 · 播完停在结尾，可点下一项"
        }

        // ===== 翻项栏 =====
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            spacing: 12

            Button {
                id: prevButton
                text: "上一项"
                enabled: viewer.pageIndex > 0
                onClicked: viewer.gotoPage(viewer.pageIndex - 1)
            }
            Label {
                font.pixelSize: 13
                text: viewer.itemCount() > 0
                      ? (viewer.pageIndex + 1) + " / " + viewer.itemCount() : "0 / 0"
            }
            SpinBox {
                from: 1; to: Math.max(1, viewer.itemCount())
                value: viewer.pageIndex + 1
                enabled: viewer.itemCount() > 1
                onValueModified: viewer.gotoPage(value - 1)
                editable: true
            }
            Button {
                id: nextButton
                text: "下一项"
                enabled: viewer.pageIndex < viewer.itemCount() - 1
                onClicked: viewer.gotoPage(viewer.pageIndex + 1)
            }
        }
    }

    // 视频播放器实例（Loader 按需创建）
    // 手动播放：autoPlay 为 false，加载后停在首帧；暂停/播完/切项/关窗均写回记忆
    Component {
        id: videoComponent

        VideoPlayer {
            source: viewer.currentItem !== null && viewer.currentIsVideo
                    ? viewer.currentItem.url : ""
            startPositionMs: viewer.startPositionMs
            autoPlay: false
            onPaused: viewer.savePositionNow()
            onFinished: viewer.savePositionNow()
        }
    }
}
