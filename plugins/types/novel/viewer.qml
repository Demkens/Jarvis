// novel 类型包 · 阅读器页（核心窗口加载本页，注入 packageView 上下文，开发文档 7.5）
// 职责：
//  - 左侧章节目录（按包清单 config.txt 的卷/章结构分组，卷标题只作分组、不可点击）；
//  - 右侧正文滚动区，点击目录跳章；
//  - 字号三档（小/中/大），不在目录与正文之外引入多余控件；
//  - 浏览记忆：{chapter: 章序号(1 基), scroll: 像素偏移}，打开时还原、翻章与滚动后自动保存。
// 边界：不解析源 txt、不直接开文件，全部经 packageView.readText() 读包内章文件；
//      包清单缺失时退化为「按文件列表顺序阅读」，仍可正常显示。
// 与核心容器的契约：
//   输入  packageView       —— 由 ViewerWindow 注入的上下文对象；
//   输出  savePositionNow() —— 容器关窗前调用，把当前章与滚动位置写入 positionJson 并落库。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    // 由核心容器注入
    property var packageView: null

    // 目录行：{kind: "volume"|"chapter", title, chapterIndex}
    property var rows: []
    // 章文件相对路径与标题（下标即章序号，0 基；记忆里按 1 基存取）
    property var chapterFiles: []
    property var chapterTitles: []

    property int currentIndex: -1
    // 字号档位：0 小 / 1 中 / 2 大
    property int fontLevel: 1
    readonly property var fontSizes: [14, 17, 20]

    implicitWidth: 900
    implicitHeight: 640

    onPackageViewChanged: loadBook()
    Component.onCompleted: if (packageView) loadBook()

    // ---- 数据加载 ----

    // 函数: 构建目录与章节清单，并还原上次阅读位置
    // 输入/输出: 无（读 page.packageView）
    function loadBook() {
        rows = []
        chapterFiles = []
        chapterTitles = []
        currentIndex = -1
        bodyText.text = ""
        if (!packageView)
            return

        var list = []
        var files = []
        var titles = []

        // 章节结构由核心解析包清单后提供（chapters/volumes），本页不解析 config.txt
        var chapters = packageView.chapters || []
        if (chapters.length > 0) {
            var volumeNames = ({})
            var vols = packageView.volumes || []
            for (var v = 0; v < vols.length; ++v)
                volumeNames[String(vols[v].index)] = vols[v].name

            var lastVolume = -1
            for (var i = 0; i < chapters.length; ++i) {
                var c = chapters[i]
                var vol = c.volume || 0
                if (vol > 0 && vol !== lastVolume) {
                    lastVolume = vol
                    list.push({ kind: "volume",
                                title: volumeNames[String(vol)] || ("第 " + vol + " 卷"),
                                chapterIndex: -1 })
                }
                files.push(c.file)
                titles.push(c.title)
                list.push({ kind: "chapter", title: c.title, chapterIndex: i })
            }
        } else {
            // 清单缺失（旧包或手工放入的文件）：按文件清单顺序阅读
            var pkgFiles = packageView.files || []
            for (var k = 0; k < pkgFiles.length; ++k) {
                files.push(pkgFiles[k].relativePath)
                titles.push(pkgFiles[k].name)
                list.push({ kind: "chapter", title: pkgFiles[k].name, chapterIndex: k })
            }
        }

        chapterFiles = files
        chapterTitles = titles
        rows = list
        restorePosition()
    }

    // 函数: 目录中某章所在行号（用于自动定位）
    // 输入: ci —— 章序号（0 基）；输出: rows 下标，未找到返回 0
    function rowIndexOf(ci) {
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].kind === "chapter" && rows[i].chapterIndex === ci)
                return i
        }
        return 0
    }

    // 函数: 还原上次阅读位置并打开该章
    // 输入/输出: 无（读 packageView.positionJson）
    function restorePosition() {
        if (chapterFiles.length === 0)
            return
        var target = 0
        var scroll = 0
        var pos = packageView.positionJson
        if (pos && pos.length > 0) {
            try {
                var parsed = JSON.parse(pos)
                if (parsed.chapter)
                    target = parsed.chapter - 1
                if (parsed.scroll)
                    scroll = parsed.scroll
            } catch (e) {
                target = 0
            }
        }
        target = Math.max(0, Math.min(target, chapterFiles.length - 1))
        openChapter(target, scroll)
    }

    // 函数: 打开指定章并设置滚动偏移
    // 输入: ci —— 章序号（0 基）；scroll —— 目标纵向偏移（像素，<=0 表示顶部）
    // 输出: 无（正文更新、目录定位、触发一次延迟保存）
    function openChapter(ci, scroll) {
        if (!packageView || ci < 0 || ci >= chapterFiles.length)
            return
        currentIndex = ci
        bodyText.text = packageView.readText(chapterFiles[ci])
        var target = Math.max(0, scroll || 0)
        // 正文高度在下一帧才确定，偏移需延后设置
        Qt.callLater(function () { contentFlick.contentY = target })
        chapterList.positionViewAtIndex(rowIndexOf(ci), ListView.Contain)
        scheduleSave()
    }

    // 函数: 立即把当前章与滚动位置写入浏览记忆（容器关窗时也会调用）
    // 输入/输出: 无
    function savePositionNow() {
        if (!packageView || currentIndex < 0)
            return
        packageView.positionJson = JSON.stringify({
            chapter: currentIndex + 1,
            scroll: Math.max(0, Math.round(contentFlick.contentY))
        })
        packageView.savePosition()
    }

    // 函数: 延迟保存（滚动/翻章频繁触发时合并写库）
    // 输入/输出: 无
    function scheduleSave() {
        saveTimer.restart()
    }

    Timer {
        id: saveTimer
        interval: 500
        repeat: false
        onTriggered: page.savePositionNow()
    }

    // ================= 视图 =================

    // 分栏与左栏内部全部使用锚点：RowLayout / ColumnLayout 在该页面上会把无最小宽度
    //   约束的子项算成 0 宽或 0 高（目录整块空白），锚点几何是确定的
    Item {
        anchors.fill: parent

        // ---------- 左：章节目录（固定 250） ----------
        Item {
            id: leftPane
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 250

            Rectangle {
                id: listHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 44
                color: "#f5f7fb"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    Text {
                        Layout.fillWidth: true
                        text: "章节目录"
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        text: page.chapterFiles.length + " 章"
                        font.pixelSize: 11
                        color: "#7a8394"
                    }
                }
            }

            ListView {
                id: chapterList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: listHeader.bottom
                anchors.bottom: parent.bottom
                clip: true
                model: page.rows

                delegate: Item {
                    id: chapterRow
                    required property var modelData
                    width: chapterList.width
                    height: chapterRow.modelData.kind === "volume" ? 32 : 30

                    Text {
                        visible: chapterRow.modelData.kind === "volume"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: chapterRow.modelData.title
                        font.pixelSize: 12
                        font.bold: true
                        color: "#4a5262"
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        visible: chapterRow.modelData.kind === "chapter"
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        anchors.topMargin: 1
                        anchors.bottomMargin: 1
                        radius: 4
                        color: chapterRow.modelData.chapterIndex === page.currentIndex
                               ? "#e8f0ff" : "transparent"

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            text: chapterRow.modelData.title
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            color: chapterRow.modelData.chapterIndex === page.currentIndex
                                   ? "#3d6ecd" : "#1f2430"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.openChapter(chapterRow.modelData.chapterIndex, 0)
                        }
                    }
                }
            }
        }

        Rectangle {
            id: separator
            anchors.left: leftPane.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: "#e3e7ee"
        }

        // ---------- 右：正文 ----------
        ColumnLayout {
            anchors.left: separator.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 44
                color: "#f5f7fb"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: page.currentIndex >= 0
                              ? (page.chapterTitles[page.currentIndex] || "")
                              : ""
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        text: "字号"
                        font.pixelSize: 11
                        color: "#7a8394"
                    }
                    Button {
                        text: "小"
                        flat: true
                        highlighted: page.fontLevel === 0
                        onClicked: page.fontLevel = 0
                    }
                    Button {
                        text: "中"
                        flat: true
                        highlighted: page.fontLevel === 1
                        onClicked: page.fontLevel = 1
                    }
                    Button {
                        text: "大"
                        flat: true
                        highlighted: page.fontLevel === 2
                        onClicked: page.fontLevel = 2
                    }
                }
            }

            Flickable {
                id: contentFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: bodyText.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                onContentYChanged: if (page.currentIndex >= 0) page.scheduleSave()

                Text {
                    id: bodyText
                    x: 20
                    y: 12
                    width: contentFlick.width - 40
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.pixelSize: page.fontSizes[page.fontLevel]
                    lineHeight: 1.45
                    color: "#1f2430"
                }
            }
        }
    }
}
