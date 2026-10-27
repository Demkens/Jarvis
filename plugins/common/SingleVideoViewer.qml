// 单视频类型（film / modelfilm）· 查看器（公共实现）
// 职责：在包内挑出唯一视频文件，交给 VideoPlayer 播放；负责续看位置的读回与写回。
// 由核心 ViewerWindow 通过 Loader 嵌入并注入：
//   property var packageView  —— PackageViewContext：
//     files（[{index,name,relativePath,size,url,media}]）、positionJson、savePosition()、title
// 续看语义：positionJson = {"index": 1, "positionMs": 毫秒}。
//   播放中每 10 秒写回一次；暂停与关窗（savePositionNow）立即写回。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "."

Item {
    id: viewer

    // 核心注入
    property var packageView: null

    // 包内第一个视频文件（files 已按相对路径排序；单视频类型包内只有一个）
    readonly property var videoFile: {
        if (!packageView)
            return null
        for (var i = 0; i < packageView.files.length; ++i) {
            if (packageView.files[i].media === "video")
                return packageView.files[i]
        }
        return null
    }

    // 打开时从浏览记忆续看（毫秒）；0 表示从头开始
    property int startPositionMs: 0

    // 恢复续看位置。
    // 注意：packageView 由外层 Loader.onLoaded 注入，晚于本组件 onCompleted 触发，
    // 因此用 onPackageViewChanged（与 ImageSequenceViewer 一致）。
    onPackageViewChanged: {
        if (packageView && packageView.positionJson.length > 0) {
            try {
                var pos = JSON.parse(packageView.positionJson)
                if (pos.positionMs !== undefined)
                    viewer.startPositionMs = Math.max(0, pos.positionMs)
            } catch (e) { /* 损坏的位置忽略，从头播放 */ }
        }
    }

    // 函数: 立即把当前播放位置写入浏览记忆
    // 输入/输出: 无；容器关窗时调用，避免 10 秒定时窗口内关窗丢失进度
    function savePositionNow() {
        if (!packageView)
            return
        packageView.positionJson = JSON.stringify({index: 1, positionMs: player.positionMs})
        packageView.savePosition()
    }

    // 定时写回：只在播放中生效，避免暂停后反复写库
    Timer {
        interval: 10000
        repeat: true
        running: player.playing
        onTriggered: viewer.savePositionNow()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 工具条：标题 + 文件名（单视频包内文件名即视频原名）
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
                Layout.maximumWidth: 420
            }
            Label {
                text: viewer.videoFile ? viewer.videoFile.name : ""
                font.pixelSize: 12
                elide: Label.ElideMiddle
                Layout.fillWidth: true
            }
        }

        // 播放区
        VideoPlayer {
            id: player
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: viewer.videoFile ? viewer.videoFile.url : ""
            startPositionMs: viewer.startPositionMs
            // 暂停即写回（含打开时自动暂停；此时位置已是续看位置，写回无副作用）
            onPaused: viewer.savePositionNow()
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
            text: "空格 播放/暂停 · ←→ 快退/快进 5 秒 · ↑↓ 音量 · F 全屏 · 点击画面也可播放/暂停"
        }
    }
}
