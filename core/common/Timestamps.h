#pragma once

#include <QDateTime>
#include <QString>

// db 时间字段统一格式：ISO-8601 UTC（YYYY-MM-DDTHH:mm:ssZ）
inline QString isoNowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}
