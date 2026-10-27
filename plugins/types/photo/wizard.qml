// photo 类型包 · 导入向导页
// 职责：类型包的声明式入口，把"图片 + 视频混合多选"的导入交互委托给 plugins/common 的公共组件；
//       本文件不含逻辑，form 由 ImportDialog 注入后由公共组件直接使用。
//       allowVideo: true 声明本类型接受视频混入（draw/comic 保持仅图片）。
import QtQuick
import "../../common"

ImageSequenceWizard {
    allowVideo: true
}
