#pragma once

#include <QString>

// 统一错误对象：跨层只传"错误码 + 用户可读消息"
struct AppError
{
    enum Code
    {
        None = 0,
        Validation, // 输入不合法
        Conflict,   // 与现有状态冲突（重名等）
        Io,         // 文件系统操作失败
        Db,         // 数据库操作失败
        Config,     // config.json 读写失败
    };

    Code code = None;
    QString message;

    bool ok() const { return code == None; }

    static AppError fail(Code code, const QString &message)
    {
        return AppError{code, message};
    }
};
