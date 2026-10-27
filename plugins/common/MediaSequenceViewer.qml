// 视频序列类型（episode）· 剧集查看器（公共实现）
// 职责：把包内视频按集号顺序组织成播放列表，负责"选集、自动连播、续看"三件事——
//   ① 打开时按浏览记忆跳到上次的集与该集进度；
//   ② 一集播完自动切下一集（最后一集播完停在结尾，不关窗、不循环）；
//   ③ 播集中每 10 秒、暂停时、切集时、关窗时写回 {"index": 集号(1 起), "positionMs": 毫秒}。
// 边界：只消费 packageView 的 files/positionJson/savePosition，不碰数据库与文件系统；
//       解码、进度条、音量、全屏全部复用 VideoPlayer，本文件不含播放内核。
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

    // 包内视频清单（按包内序号排序；只取 media 为 "video" 的条目）
    readonly property var videos: {
        var out = []
        if (!packageView)
            return out
        for (var i = 0; i < packageView.files.length; ++i) {
            if (packageView.files[i].media === "video")
                out.push(packageView.files[i])
        }
        return out
    }

    // 当前集（0 基；界面与记忆统一显示为 +1 的集号）
    property int currentIndex: 0

    // 当前集的起播位置（毫秒）；切集时归零，避免沿用上一集的续看位置
    property int startPositionMs: 0

    // 媒体加载完成后是否自动起播：初次打开为 false（等用户点播），切集/连播为 true
    property bool autoPlay: false

    // 恢复续看位置：{"index": 集号(1 起), "positionMs": 毫秒}。
    // packageView 由外层 Loader.onLoaded 注入，晚于本组件 onCompleted，故用 onPackageViewChanged
    onPackageViewChanged: {
        var restoredIndex = 0
        var restoredPosition = 0
        if (packageView && packageView.positionJson.length > 0) {
            try {
                var pos = JSON.parse(packageView.positionJson)
                if (pos.index !== undefined)
                    // 上界 clamp：包内文件被外部删减后记忆集号不得越界
                    restoredIndex = Math.max(0, Math.min(pos.index - 1, viewer.videos.length - 1))
                if (pos.positionMs !== undefined)
                    restoredPosition = Math.max(0, pos.positionMs)
            } catch (e) { /* 损坏的位置忽略，从头播放 */ }
        }
        // 先写起播位置再写集号：集号变化会触发换源与异步加载，
        // 保证媒体就绪时 startPositionMs 已是最终值
        viewer.startPositionMs = restoredPosition
        viewer.currentIndex = restoredIndex
        viewer.autoPlay = false
    }

    // 函数: 立即把当前集与播放位置写入浏览记忆
    // 输入/输出: 无；播放中定时、暂停、切集、关窗时调用
    function savePositionNow() {
        if (!packageView)
            return
        packageView.positionJson = JSON.stringify({
            index: viewer.currentIndex + 1,
            positionMs: player.positionMs
        })
        packageView.savePosition()
    }

    // 函数: 切换到指定集并开始播放
    // 输入: index —— 目标集（0 基，越界自动钳制）；autoplay —— 切集后是否自动起播
    // 输出: 无（先写回当前集进度，再换源）
    function gotoEpisode(index, autoplay) {
        var n = Math.max(0, Math.min(index, viewer.videos.length - 1))
        if (n === viewer.currentIndex)
            return
        viewer.savePositionNow()     // 记录离开时该集的进度
        viewer.startPositionMs = 0   // 新集从头开始
        viewer.autoPlay = autoplay
        viewer.currentIndex = n      // 集号变化 → 换源（异步加载）
    }

    // 函数: 播放下一集；已是最后一集则停在结尾
    // 输入/输出: 无
    function playNext() {
        if (viewer.currentIndex >= viewer.videos.length - 1) {
            viewer.savePositionNow() // 最后一集结束：记住结尾位置
            return
        }
        viewer.gotoEpisode(viewer.currentIndex + 1, true)
    }

    // 播放中每 10 秒写回一次；暂停、切集与关窗由事件立即写回
    Timer {
        interval: 10000
        repeat: true
        running: player.playing
        onTriggered: viewer.savePositionNow()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ===== 左：播放区 =====
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 工具条：剧名 + 当前集与总集数 + 当前集文件名
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 8
                Layout.bottomMargin: 6
                spacing: 10

                Label {
                    text: packageView ? packageView.title : ""
                    font.pixelSize: 14
                    font.bold: true
                    elide: Label.ElideRight
                    Layout.maximumWidth: 320
                }
                Label {
                    visible: viewer.videos.length > 0
                    text: "第 " + (viewer.currentIndex + 1) + " / " + viewer.videos.length + " 集"
                    font.pixelSize: 12
                    color: "#5c6472"
                }
                Label {
                    text: viewer.videos.length > 0
                          ? viewer.videos[viewer.currentIndex].name : ""
                    font.pixelSize: 12
                    elide: Label.ElideMiddle
                    Layout.fillWidth: true
                }
            }

            // 播放内核（复用公共组件；换集即换 source）
            VideoPlayer {
                id: player
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: viewer.videos.length > 0
                        ? viewer.videos[viewer.currentIndex].url : ""
                startPositionMs: viewer.startPositionMs
                autoPlay: viewer.autoPlay
                onPaused: viewer.savePositionNow()
                onFinished: viewer.playNext()
            }

            // 提示条
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 4
                Layout.bottomMargin: 6
                font.pixelSize: 11
                color: "#5c6472"
                text: "空格 播放/暂停 · ←→ 快退/快进 5 秒 · ↑↓ 音量 · F 全屏 · 播完自动进入下一集"
            }
        }

        // ===== 右：集列表（单集包不显示） =====
        Rectangle {
            id: episodePanel
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            visible: viewer.videos.length > 1
            color: "#f3f5f9"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Label {
                    text: "集列表（共 " + viewer.videos.length + " 集）"
                    font.pixelSize: 12
                    font.bold: true
                }

                ListView {
                    id: episodeList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: viewer.videos
                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: episodeRow
                        required property int index
                        required property var modelData
                        width: ListView.view.width
                        highlighted: episodeRow.index === viewer.currentIndex
                        onClicked: viewer.gotoEpisode(episodeRow.index, true)

                        contentItem: ColumnLayout {
                            spacing: 1

                            Label {
                                text: "第 " + (episodeRow.index + 1) + " 集"
                                font.pixelSize: 12
                                font.bold: episodeRow.highlighted
                            }
                            Label {
                                text: episodeRow.modelData.name
                                font.pixelSize: 11
                                color: "#5c6472"
                                elide: Label.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }
}
