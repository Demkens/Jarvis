// 查看器窗口（核心容器）：按类型包声明的 viewer.qml 加载图集页，注入 packageView 上下文。
// 类型包只提供页面内容；窗口、上下文制造、路径拼接都在核心（开发文档 3.5 / 7.3）。
import QtQuick

Window {
    id: window

    // Main 设置后打开
    property int packageId: 0
    property var packageView: null

    // 打开失败的可见反馈（通知 Main 状态条，避免静默无效果）
    signal openFailed(string message)

    title: packageView ? "查看 - " + packageView.title : "查看"
    width: 1000
    height: 760
    minimumWidth: 720
    minimumHeight: 560

    function openPackage(id) {
        window.packageId = id
        window.packageView = typeEngine.createPackageView(id)
        if (window.packageView === null) {
            console.warn("无法打开数据包上下文 #" + id)
            window.openFailed("无法打开数据包 #" + id + "（类型包缺失或数据不可读）")
            return
        }
        viewerLoader.active = true
        window.show()
        window.requestActivate()
    }

    onClosing: function () {
        // 关闭前先让类型页把当前浏览位置同步进 positionJson（各类型语义不同：
        // 画作是 {index}，小说是 {chapter, scroll}），再由核心统一落库。
        // 类型页未提供 savePositionNow 时，退化为保存已写入的 positionJson，
        // 避免核心容器硬编码某一类型的记忆结构。
        const v = viewerLoader.item
        if (v && typeof v.savePositionNow === "function")
            v.savePositionNow()
        else if (packageView !== null)
            packageView.savePosition()
        viewerLoader.active = false
        packageView = null
    }

    Loader {
        id: viewerLoader
        anchors.fill: parent
        active: false
        source: packageView ? typeEngine.viewerUrl(packageView.typeForm) : ""
        onLoaded: item.packageView = window.packageView
    }
}
