// 短笔记服务（M8a）：
// 职责：
// - 维护当前库的笔记元数据（note 表：标题与时间），不存正文；
// - 笔记正文以 Markdown（UTF-8）存于 envs/<库名>/note/<id>.md，由本服务独占读写；
// - 提供正文与元数据的一致性规则：文件是正文唯一真相，数据库只做索引；
// - 打开笔记时把文件 mtime 交给调用方作为基线，保存时据此发现外部改动并拒绝静默覆盖。
// 边界：
// - 只依赖 LibraryService（当前库与 DbAccess）与 AppPaths（拼 note 目录）；
// - 不触碰 TypeEngine/EventBus，不解析 Markdown，不处理包链接（file_note 属 M8b）。
// - 数据库写操作走事务；文件写入使用 QSaveFile 原子替换。
#pragma once

#include <QObject>
#include <QVariantMap>

#include "common/AppError.h"

class AppPaths;
class LibraryService;

class NoteService : public QObject
{
    Q_OBJECT

public:
    // 构造：library 提供当前库名与连接，paths 提供 note 目录位置；两者均不可为空且生命周期需覆盖本对象。
    explicit NoteService(LibraryService *library, AppPaths *paths, QObject *parent = nullptr);

    // 函数: 列出当前库的全部笔记（不含正文）
    // 输入: 无
    // 输出: {ok, total, items:[{id, title, createdTime, updatedTime, missing}]}
    //       按 updated_time 倒序、id 倒序；missing 表示对应的 <id>.md 不存在。
    //       库未打开时返回 ok=true 的空列表，由页面展示空态。
    Q_INVOKABLE QVariantMap listNotes();

    // 函数: 读取单条笔记（元数据 + 正文）
    // 输入: noteId —— 笔记 id（须为正整数）
    // 输出: {ok, note:{id, title, content, createdTime, updatedTime, mtime, missing}}
    //       mtime 为当前文件最后修改时间（毫秒）；文件缺失时为 -1，missing=true。
    //       正文读取失败（文件存在但不可读）返回 ok=false。
    Q_INVOKABLE QVariantMap note(qint64 noteId);

    // 函数: 新建笔记（数据库行 + Markdown 文件）
    // 输入: title —— 标题（可空，存库前去首尾空白）；content —— Markdown 正文
    // 输出: {ok, id, mtime}；标题与正文同时为空时拒绝，返回 ok=false。
    //       先插入数据库行取得 id，再写 <id>.md；文件写入失败则回滚并清理文件。
    Q_INVOKABLE QVariantMap createNote(const QString &title, const QString &content);

    // 函数: 保存已有笔记的标题与正文
    // 输入: noteId —— 笔记 id；title/content —— 新元数据与正文；
    //       baselineMtime —— 打开笔记时 note() 返回的 mtime（文件缺失时为 -1）；
    //       force —— true 表示用户已确认覆盖外部改动
    // 输出: {ok, mtime}；若文件在打开后被外部修改、删除或出现而 force=false，
    //       返回 {ok:false, conflict:true, message}，由界面让用户选择覆盖或取消。
    //       正文先原子写盘、再更新数据库元数据；写盘失败不触碰数据库。
    Q_INVOKABLE QVariantMap saveNote(qint64 noteId, const QString &title, const QString &content,
                                     qint64 baselineMtime, bool force);

    // 函数: 删除笔记（数据库行 + Markdown 文件）
    // 输入: noteId —— 笔记 id
    // 输出: {ok}。先删除数据库行并提交，再尽力删除文件；文件删除失败只记录警告，不回滚。
    Q_INVOKABLE QVariantMap deleteNote(qint64 noteId);

private:
    // 函数: 校验当前库可用
    // 输入/输出: error —— 失败时的错误出口（可空）；返回 true 表示库已打开
    bool requireLibrary(AppError *error) const;

    // 函数: 笔记正文文件路径
    // 输入: noteId；输出: <envs>/<库名>/note/<id>.md
    QString noteFilePath(qint64 noteId) const;

    // 函数: 确保笔记目录存在（按需创建）
    // 输入/输出: error —— 失败原因；返回 true 表示目录可用
    bool ensureNoteDir(AppError *error) const;

    // 函数: 读取 note 表的一行
    // 输入: noteId；输出: row —— 列 id/title/created_time/updated_time；返回 false 表示不存在或查询失败
    bool readNoteRow(qint64 noteId, QVariantMap *row, AppError *error) const;

    // 函数: 读取笔记文件
    // 输入: noteId
    // 输出: content —— UTF-8 正文；mtime —— 文件最后修改毫秒（缺失为 -1）；
    //       missing —— 文件是否不存在
    // 返回 false 仅表示文件存在但读取失败（Io）。
    bool readFile(qint64 noteId, QString *content, qint64 *mtime, bool *missing,
                  AppError *error) const;

    // 函数: 原子写入笔记文件（QSaveFile）
    // 输入: noteId、content
    // 输出: mtime —— 写入后的文件最后修改毫秒
    // 返回 false 表示打开/写入/提交任一步失败，已填充 error。
    bool writeFile(qint64 noteId, const QString &content, qint64 *mtime, AppError *error) const;

    // 成功/失败结果封装（QML 友好）
    static QVariantMap okResult();
    static QVariantMap failResult(const AppError &error);
    static QVariantMap conflictResult(const QString &message);

    LibraryService *m_library;
    AppPaths *m_paths;
};
