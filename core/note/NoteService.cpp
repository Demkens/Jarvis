#include "NoteService.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVariantList>

#include "common/Timestamps.h"
#include "library/AppPaths.h"
#include "library/DbAccess.h"
#include "library/LibraryService.h"

namespace {

// 把局部错误写入调用方出口（出口可空）
void setError(AppError *error, AppError::Code code, const QString &message)
{
    if (error != nullptr)
        *error = AppError::fail(code, message);
}

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

} // namespace

NoteService::NoteService(LibraryService *library, AppPaths *paths, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_paths(paths)
{
}

QVariantMap NoteService::okResult()
{
    return QVariantMap{{QStringLiteral("ok"), true}};
}

QVariantMap NoteService::failResult(const AppError &error)
{
    return QVariantMap{{QStringLiteral("ok"), false},
                       {QStringLiteral("message"), error.message}};
}

QVariantMap NoteService::conflictResult(const QString &message)
{
    return QVariantMap{{QStringLiteral("ok"), false},
                       {QStringLiteral("conflict"), true},
                       {QStringLiteral("message"), message}};
}

bool NoteService::requireLibrary(AppError *error) const
{
    if (!m_library->isOpen() || m_library->currentName().isEmpty()) {
        setError(error, AppError::Validation, QStringLiteral("当前没有打开的库"));
        return false;
    }
    return true;
}

QString NoteService::noteFilePath(qint64 noteId) const
{
    return m_paths->noteDir(m_library->currentName()) + QLatin1Char('/')
           + QString::number(noteId) + QStringLiteral(".md");
}

bool NoteService::ensureNoteDir(AppError *error) const
{
    const QString dirPath = m_paths->noteDir(m_library->currentName());
    QDir dir(dirPath);
    if (dir.exists() || dir.mkpath(QStringLiteral(".")))
        return true;

    setError(error, AppError::Io,
             QStringLiteral("无法创建笔记目录：%1").arg(nativePath(dirPath)));
    return false;
}

bool NoteService::readNoteRow(qint64 noteId, QVariantMap *row, AppError *error) const
{
    const bool found = m_library->dbAccess().selectOne(
        QStringLiteral("SELECT id, title, created_time, updated_time FROM note WHERE id = :id"),
        {{QStringLiteral("id"), noteId}}, *row, error);
    if (found)
        return true;

    // selectOne 未命中时不填错误，这里补一条可读消息
    if (error != nullptr && error->ok())
        *error = AppError::fail(AppError::Validation,
                                QStringLiteral("笔记不存在：#%1").arg(noteId));
    return false;
}

bool NoteService::readFile(qint64 noteId, QString *content, qint64 *mtime, bool *missing,
                           AppError *error) const
{
    const QString path = noteFilePath(noteId);
    const QFileInfo info(path);

    if (!info.exists()) {
        if (content != nullptr)
            content->clear();
        if (mtime != nullptr)
            *mtime = -1;
        if (missing != nullptr)
            *missing = true;
        return true;
    }

    if (missing != nullptr)
        *missing = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, AppError::Io,
                 QStringLiteral("无法读取笔记文件：%1").arg(nativePath(path)));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        setError(error, AppError::Io,
                 QStringLiteral("读取笔记文件失败：%1").arg(nativePath(path)));
        return false;
    }

    if (content != nullptr)
        *content = QString::fromUtf8(bytes);
    if (mtime != nullptr)
        *mtime = info.lastModified().toMSecsSinceEpoch();
    return true;
}

bool NoteService::writeFile(qint64 noteId, const QString &content, qint64 *mtime,
                            AppError *error) const
{
    const QString path = noteFilePath(noteId);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, AppError::Io,
                 QStringLiteral("无法打开笔记文件：%1").arg(nativePath(path)));
        return false;
    }

    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        setError(error, AppError::Io,
                 QStringLiteral("写入笔记文件失败：%1").arg(nativePath(path)));
        return false;
    }
    if (!file.commit()) {
        setError(error, AppError::Io,
                 QStringLiteral("提交笔记文件失败：%1").arg(nativePath(path)));
        return false;
    }

    if (mtime != nullptr)
        *mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    return true;
}

QVariantMap NoteService::listNotes()
{
    if (!m_library->isOpen() || m_library->currentName().isEmpty()) {
        QVariantMap out = okResult();
        out.insert(QStringLiteral("total"), 0);
        out.insert(QStringLiteral("items"), QVariantList{});
        return out;
    }

    AppError error;
    const QList<QVariantMap> rows = m_library->dbAccess().select(
        QStringLiteral("SELECT id, title, created_time, updated_time FROM note "
                       "ORDER BY updated_time DESC, id DESC"),
        {}, &error);
    if (!error.ok())
        return failResult(error);

    QVariantList items;
    items.reserve(rows.size());
    for (const QVariantMap &row : rows) {
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        QVariantMap item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("title"), row.value(QStringLiteral("title")).toString());
        item.insert(QStringLiteral("createdTime"),
                    row.value(QStringLiteral("created_time")).toString());
        item.insert(QStringLiteral("updatedTime"),
                    row.value(QStringLiteral("updated_time")).toString());
        item.insert(QStringLiteral("missing"), !QFileInfo::exists(noteFilePath(id)));
        items.append(item);
    }

    QVariantMap out = okResult();
    out.insert(QStringLiteral("total"), items.size());
    out.insert(QStringLiteral("items"), items);
    return out;
}

QVariantMap NoteService::note(qint64 noteId)
{
    AppError error;
    if (!requireLibrary(&error))
        return failResult(error);
    if (noteId <= 0) {
        setError(&error, AppError::Validation,
                 QStringLiteral("笔记 id 非法：#%1").arg(noteId));
        return failResult(error);
    }

    QVariantMap row;
    if (!readNoteRow(noteId, &row, &error))
        return failResult(error);

    QString content;
    qint64 mtime = -1;
    bool missing = false;
    if (!readFile(noteId, &content, &mtime, &missing, &error))
        return failResult(error);

    QVariantMap noteData;
    noteData.insert(QStringLiteral("id"), noteId);
    noteData.insert(QStringLiteral("title"), row.value(QStringLiteral("title")).toString());
    noteData.insert(QStringLiteral("content"), content);
    noteData.insert(QStringLiteral("createdTime"),
                    row.value(QStringLiteral("created_time")).toString());
    noteData.insert(QStringLiteral("updatedTime"),
                    row.value(QStringLiteral("updated_time")).toString());
    noteData.insert(QStringLiteral("mtime"), mtime);
    noteData.insert(QStringLiteral("missing"), missing);

    QVariantMap out = okResult();
    out.insert(QStringLiteral("note"), noteData);
    return out;
}

QVariantMap NoteService::createNote(const QString &title, const QString &content)
{
    AppError error;
    if (!requireLibrary(&error))
        return failResult(error);

    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty() && content.trimmed().isEmpty()) {
        setError(&error, AppError::Validation,
                 QStringLiteral("标题与正文不能同时为空"));
        return failResult(error);
    }
    if (!ensureNoteDir(&error))
        return failResult(error);

    DbAccess &db = m_library->dbAccess();
    if (!db.transaction(&error))
        return failResult(error);

    const QString now = isoNowUtc();
    qint64 id = 0;
    if (!db.execute(
            QStringLiteral("INSERT INTO note (title, created_time, updated_time) "
                           "VALUES (:title, :created, :updated)"),
            {{QStringLiteral("title"), trimmedTitle},
             {QStringLiteral("created"), now},
             {QStringLiteral("updated"), now}},
            &id, &error)) {
        db.rollback();
        return failResult(error);
    }

    qint64 mtime = -1;
    if (!writeFile(id, content, &mtime, &error)) {
        db.rollback();
        // QSaveFile 失败时一般不留正式文件，这里仅兜底清理
        QFile::remove(noteFilePath(id));
        return failResult(error);
    }

    if (!db.commit(&error)) {
        // 数据库未提交，回滚并清理刚写入的文件，避免产生孤立正文
        db.rollback();
        QFile::remove(noteFilePath(id));
        return failResult(error);
    }

    QVariantMap out = okResult();
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("mtime"), mtime);
    return out;
}

QVariantMap NoteService::saveNote(qint64 noteId, const QString &title, const QString &content,
                                  qint64 baselineMtime, bool force)
{
    AppError error;
    if (!requireLibrary(&error))
        return failResult(error);
    if (noteId <= 0) {
        setError(&error, AppError::Validation,
                 QStringLiteral("笔记 id 非法：#%1").arg(noteId));
        return failResult(error);
    }

    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty() && content.trimmed().isEmpty()) {
        setError(&error, AppError::Validation,
                 QStringLiteral("标题与正文不能同时为空"));
        return failResult(error);
    }

    QVariantMap row;
    if (!readNoteRow(noteId, &row, &error))
        return failResult(error);

    // 外部改动检测：文件的存在性与 mtime 必须与打开时一致，否则交给用户裁决
    const QString path = noteFilePath(noteId);
    const QFileInfo info(path);
    const bool exists = info.exists();
    if (!force) {
        if (baselineMtime < 0 && exists)
            return conflictResult(QStringLiteral("笔记文件在打开后出现，保存将覆盖外部内容"));
        if (baselineMtime >= 0 && !exists)
            return conflictResult(QStringLiteral("笔记文件已被外部删除，保存将重新创建"));
        if (exists && baselineMtime >= 0
            && info.lastModified().toMSecsSinceEpoch() != baselineMtime) {
            return conflictResult(QStringLiteral("笔记文件已被外部修改，保存将覆盖外部内容"));
        }
    }

    if (!ensureNoteDir(&error))
        return failResult(error);

    qint64 mtime = -1;
    if (!writeFile(noteId, content, &mtime, &error))
        return failResult(error);

    DbAccess &db = m_library->dbAccess();
    if (!db.transaction(&error))
        return failResult(error);
    if (!db.execute(QStringLiteral("UPDATE note SET title = :title, updated_time = :updated "
                                   "WHERE id = :id"),
                    {{QStringLiteral("title"), trimmedTitle},
                     {QStringLiteral("updated"), isoNowUtc()},
                     {QStringLiteral("id"), noteId}},
                    &error)) {
        db.rollback();
        return failResult(error);
    }
    if (!db.commit(&error)) {
        db.rollback();
        return failResult(error);
    }

    QVariantMap out = okResult();
    out.insert(QStringLiteral("mtime"), mtime);
    return out;
}

QVariantMap NoteService::deleteNote(qint64 noteId)
{
    AppError error;
    if (!requireLibrary(&error))
        return failResult(error);
    if (noteId <= 0) {
        setError(&error, AppError::Validation,
                 QStringLiteral("笔记 id 非法：#%1").arg(noteId));
        return failResult(error);
    }

    QVariantMap row;
    if (!readNoteRow(noteId, &row, &error))
        return failResult(error);

    DbAccess &db = m_library->dbAccess();
    if (!db.transaction(&error))
        return failResult(error);
    if (!db.execute(QStringLiteral("DELETE FROM note WHERE id = :id"),
                    {{QStringLiteral("id"), noteId}}, &error)) {
        db.rollback();
        return failResult(error);
    }
    if (!db.commit(&error)) {
        db.rollback();
        return failResult(error);
    }

    // 记录已删除；文件删除失败只留孤立文件，不反向恢复记录（避免笔记“复活”）
    const QString path = noteFilePath(noteId);
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        qWarning().noquote() << QStringLiteral("[note] 笔记记录已删除但文件清理失败：%1")
                                    .arg(nativePath(path));
    }
    return okResult();
}
