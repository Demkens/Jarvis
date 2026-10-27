// 视频播放器公共组件（film / modelfilm 等单视频类型共用）
// 职责：封装 Qt Multimedia 的播放内核与最小播放控件（播放/暂停、进度、音量、全屏、键盘快捷键），
//       对上层只暴露"给视频源 + 起播位置，取当前毫秒位置"的窄接口。
// 边界：不读写浏览记忆、不碰数据库；续看位置的解析与写回由 SingleVideoViewer 负责。
// 契约：
//   输入  source           —— 视频文件 file:// URL（空串 = 无视频）
//         startPositionMs  —— 媒体加载完成后要跳转到的毫秒位置（只应用一次）
//         volume           —— 音量 0..1
//   输出  playing / positionMs / durationMs / seekable（只读，供上层保存进度）
//         paused()         —— 进入暂停态时发出（上层据此写回续看位置）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia

Item {
    id: root

    property string source: ""
    property int startPositionMs: 0
    property real volume: 0.8

    // 上一次播放状态：用于区分「用户从播放态暂停」与「加载完成的自动暂停」。
    // 只有前者才发出 paused()，避免打开后立刻关窗把续看位置写回 0。
    property int lastState: MediaPlayer.StoppedState

    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property int positionMs: player.position
    readonly property int durationMs: player.duration
    readonly property bool seekable: player.seekable

    // 输出: 进入暂停态（含手动暂停、拖动后暂停、加载完成自动暂停）
    signal paused()

    // 函数: 开始播放
    // 输入/输出: 无
    function play() {
        player.play()
        root.forceActiveFocus()
    }

    // 函数: 暂停播放
    // 输入/输出: 无
    function pause() {
        player.pause()
    }

    // 函数: 播放/暂停切换
    // 输入/输出: 无
    function togglePlay() {
        if (root.playing)
            root.pause()
        else
            root.play()
    }

    // 函数: 相对跳转（负数为快退）
    // 输入: deltaMs —— 相对毫秒数；输出: 无（不可 seek 时忽略）
    function seekBy(deltaMs) {
        if (!player.seekable)
            return
        var target = player.position + deltaMs
        target = Math.max(0, Math.min(target, Math.max(0, player.duration)))
        player.position = target
    }

    // 函数: 窗口全屏切换（查看器窗口由核心提供，故直接操作所在 Window）
    // 输入/输出: 无
    function toggleFullscreen() {
        var win = root.Window.window
        if (!win)
            return
        win.visibility = (win.visibility === Window.FullScreen) ? Window.Windowed
                                                                : Window.FullScreen
    }

    // 函数: 毫秒 → mm:ss / h:mm:ss
    // 输入: ms —— 毫秒；输出: 可读时长文本
    function formatTime(ms) {
        if (!ms || ms < 0)
            return "00:00"
        var total = Math.floor(ms / 1000)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(n) { return n < 10 ? "0" + n : String(n) }
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s)
    }

    focus: true

    // 快捷键：空格 播放/暂停；←→ 快退/快进 5 秒；↑↓ 音量；F 全屏
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Space) {
            root.togglePlay()
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            root.seekBy(-5000)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            root.seekBy(5000)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            root.volume = Math.min(1.0, root.volume + 0.05)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            root.volume = Math.max(0.0, root.volume - 0.05)
            event.accepted = true
        } else if (event.key === Qt.Key_F) {
            root.toggleFullscreen()
            event.accepted = true
        }
    }

    // 播放内核：
    // Qt 6 的 VideoOutput 没有 source 属性，画面接入方向是反的——
    // 由播放器通过 videoOutput 属性指向 VideoOutput 元素（Qt 6.9 的 QML 契约）
    MediaPlayer {
        id: player
        source: root.source
        videoOutput: output
        audioOutput: AudioOutput {
            volume: root.volume
        }

        // 媒体就绪后：应用续看位置并保持暂停，等用户主动播放
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia) {
                if (root.startPositionMs > 0)
                    player.position = root.startPositionMs
                player.pause()
            }
        }

        onPlaybackStateChanged: {
            // 仅「播放 → 暂停」视为用户暂停（加载完成后的自动 pause 不写回位置）
            if (playbackState === MediaPlayer.PausedState
                    && root.lastState === MediaPlayer.PlayingState)
                root.paused()
            root.lastState = playbackState
        }
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    // 点击画面：播放/暂停（并抢焦点，保证快捷键可用）
    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.forceActiveFocus()
            root.togglePlay()
        }
    }

    // 无视频源、加载中、播放失败三种状态的可见反馈
    Text {
        anchors.centerIn: parent
        visible: root.source === ""
        text: "包内没有可播放的视频"
    }
    Text {
        anchors.centerIn: parent
        visible: root.source !== "" && player.mediaStatus === MediaPlayer.LoadingMedia
        text: "加载中…"
    }
    Text {
        anchors.centerIn: parent
        visible: player.error !== MediaPlayer.NoError
        color: "#c0341d"
        text: "视频无法播放（实体库可能已离线，或缺少该格式的解码器）"
    }

    // 控制条：半透明浮层，避免遮挡画面比例
    Rectangle {
        id: controlBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 46
        color: Qt.rgba(0, 0, 0, 0.55)

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10

            Button {
                text: root.playing ? "暂停" : "播放"
                onClicked: root.togglePlay()
            }

            Label {
                text: root.formatTime(root.positionMs)
                color: "white"
                font.pixelSize: 12
            }

            Slider {
                id: progress
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.durationMs)
                value: root.positionMs
                enabled: root.seekable
                // 拖动时写入播放位置；value 的绑定仍指向 position，不会被写入打断
                onMoved: if (root.seekable) player.position = value
            }

            Label {
                text: root.formatTime(root.durationMs)
                color: "white"
                font.pixelSize: 12
            }

            Button {
                text: "音量"
                flat: true
                onClicked: root.volume = root.volume > 0 ? 0 : 0.8
            }
            Slider {
                Layout.preferredWidth: 90
                from: 0
                to: 1
                value: root.volume
                onMoved: root.volume = value
            }

            Button {
                text: "全屏"
                onClicked: root.toggleFullscreen()
            }
        }
    }
}
