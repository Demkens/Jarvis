#pragma once

#include <QString>

// 日志系统（开发文档 3.7：一期分级日志写 envs/logs/，按日滚动）。
// 职责：以 qInstallMessageHandler 统一接管应用全部诊断输出（C++ 的 qDebug/qWarning/qCritical
//       与 QML 的 console.log/warn/error 均经此单一出口），按级别阈值过滤后按日滚动落盘，
//       同时回显到 stderr 便于开发期即时观察。
// 级别（高→低）：fatal > critical > warning > info > debug，默认 info；debug 默认不落盘。
class Logging
{
public:
    // 安装消息处理器
    // 输入: envsRoot —— envs 根目录，日志落于其下 logs/；级别由环境变量 JARVIS_LOG_LEVEL
    //       (debug/info/warning/critical) 覆盖，未设置或非法则用默认 info
    // 输出: 无；日志目录创建失败时降级为「仅 stderr」并就地提示（不中断启动）
    static void install(const QString &envsRoot);
};
