// episode 类型包 · 导入向导页
// 职责：类型包的声明式入口，把多视频（一季多集）导入交互委托给 plugins/common 的公共组件；
//       本文件不含逻辑，form 由 ImportDialog 注入后由公共组件直接使用。
//       不设 maxFiles（默认 0 = 不限）：用户一次多选整季的集文件，按文件名自然序编号入包。
import QtQuick
import "../../common"

MediaWizard {}
