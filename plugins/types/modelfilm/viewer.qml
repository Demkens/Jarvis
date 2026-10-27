// modelfilm 类型包 · 视频查看器
// 职责：类型包的声明式入口，把播放与续看交互委托给 plugins/common 的公共组件；
//       本文件不含逻辑，packageView 由 ViewerWindow 注入后由公共组件直接消费。
import QtQuick
import "../../common"

SingleVideoViewer {}
