// 查看器窗口（核心容器）：按类型包声明的 viewer.qml 加载图集页，注入 packageView 上下文。
// 类型包只提供页面内容；窗口、上下文制造、路径拼接都在核心（开发文档 3.5 / 7.3）。
import QtQuick

Window {
    id: window

    // Main 设置后打开
    property int packageId: 0
    property var packageView: null

    title: packageView ? "查看 - " + packageView.title : "查看"
    width: 1000
    height: 760
    minimumWidth: 720
    minimumHeight: 560
    color: "#1e1e1e"

    function openPackage(id) {
        window.packageId = id
        window.packageView = typeEngine.createPackageView(id)
        if (window.packageView === null) {
            console.warn("无法打开数据包上下文 #" + id)
            return
        }
        viewerLoader.active = true
        window.show()
        window.requestActivate()
    }

    onClosing: function () {
        // 关闭前再保存一次最新位置
        if (packageView !== null)
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
