#pragma once

#include <QObject>

// 事件总线：跨模块协作的唯一松耦合通道。
// ViewModel/服务只订阅事件刷新自身，不互相直接调用。
class EventBus : public QObject
{
    Q_OBJECT

public:
    explicit EventBus(QObject *parent = nullptr);

signals:
    // 当前库已切换（库名为空表示关闭/无当前库）
    void librarySwitched(const QString &dbName);

    // ---- 包事件（M2，开发文档 3.6）----
    // 导入事务提交后广播
    void packageCreated(qint64 fileId, const QString &typeForm);
    // 标题/评分等元数据修改后广播（浏览记忆写回不触发）
    void packageUpdated(qint64 fileId);
    // db 行删除且实体文件清理完成后广播
    void packageDeleted(qint64 fileId);
};
