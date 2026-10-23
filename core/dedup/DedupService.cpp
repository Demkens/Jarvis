#include "DedupService.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QVariant>

#include <utility>

#include "common/MediaTypes.h"
#include "dedup/ContentHasher.h"
#include "dedup/ImageHasher.h"
#include "library/DbAccess.h"
#include "library/LibraryConfig.h"
#include "library/LibraryService.h"
#include "storage/StorageService.h"

namespace {

// SQLite 默认参数上限约 999，分批查询避免超限
constexpr int kSizeQueryChunk = 500;
// 指纹算法版本：content_hash 为 SHA-256，感知指纹为 64 位 aHash/dHash/pHash；换算法时递增
constexpr int kFingerprintAlgoVersion = 1;

// 把 query 中的 forms 规整为去空、去重的列表
QStringList formsFromQuery(const QVariantMap &query)
{
    QStringList forms;
    const QVariantList list = query.value(QStringLiteral("forms")).toList();
    for (const QVariant &value : list) {
        const QString form = value.toString().trimmed();
        if (!form.isEmpty() && !forms.contains(form))
            forms.append(form);
    }
    return forms;
}

} // namespace

DedupService::DedupService(LibraryService *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
}

DedupService::~DedupService()
{
    cancelCurrent();
    waitForPreviousThread();
}

QVariantMap DedupService::precheck(const QVariantList &files)
{
    if (m_library == nullptr || !m_library->isOpen())
        return fail(QStringLiteral("没有打开的库，无法查重"));

    struct SourceEntry
    {
        QString path;
        qint64 size = 0;
    };

    QList<SourceEntry> entries;
    QSet<qint64> sizeSet;
    QVariantList skipped;

    for (const QVariant &value : files) {
        const QString path = value.toString();
        const QFileInfo info(path);
        if (!info.isFile()) {
            skipped.append(QVariantMap{{QStringLiteral("path"), path},
                                       {QStringLiteral("reason"), QStringLiteral("不是文件")}});
            continue;
        }
        if (!MediaTypes::isImageFile(path)) {
            skipped.append(QVariantMap{{QStringLiteral("path"), path},
                                       {QStringLiteral("reason"),
                                        QStringLiteral("非图片，未参与图片查重")}});
            continue;
        }
        entries.append(SourceEntry{info.absoluteFilePath(), info.size()});
        sizeSet.insert(info.size());
    }

    if (entries.isEmpty()) {
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("checked"), 0},
            {QStringLiteral("files"), QVariantList{}},
            {QStringLiteral("skipped"), skipped},
            {QStringLiteral("hasDuplicates"), false},
        };
    }

    // 只取同尺寸候选：字节级完全相同的文件必然同尺寸，尺寸不同可直接排除
    const QList<qint64> sizes = sizeSet.values();
    QHash<qint64, QList<QVariantMap>> candidatesBySize;
    for (int start = 0; start < sizes.size(); start += kSizeQueryChunk) {
        const QList<qint64> chunk = sizes.mid(start, kSizeQueryChunk);
        QStringList placeholders;
        QVariantMap params;
        for (int index = 0; index < chunk.size(); ++index) {
            const QString key = QStringLiteral("s%1").arg(index);
            placeholders.append(QStringLiteral(":") + key);
            params.insert(key, chunk.at(index));
        }

        const QString sql =
            QStringLiteral("SELECT p.id AS file_id, p.relative_path, p.size_bytes, "
                           "p.content_hash, f.title, t.name AS type_name "
                           "FROM photohash p "
                           "JOIN file f ON f.id = p.id "
                           "JOIN type t ON t.id = f.type_id "
                           "WHERE p.size_bytes IN (%1) "
                           "AND p.content_hash IS NOT NULL AND p.content_hash <> ''")
                .arg(placeholders.join(QLatin1Char(',')));

        AppError error;
        const QList<QVariantMap> rows = m_library->dbAccess().select(sql, params, &error);
        if (!error.ok())
            return fail(error.message);
        for (const QVariantMap &row : rows)
            candidatesBySize[row.value(QStringLiteral("size_bytes")).toLongLong()].append(row);
    }

    QVariantList checked;
    bool hasDuplicates = false;

    for (const SourceEntry &entry : entries) {
        const QList<QVariantMap> candidates = candidatesBySize.value(entry.size);
        QVariantMap item{
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("size"), entry.size},
            {QStringLiteral("hashed"), false},
            {QStringLiteral("contentHash"), QString()},
            {QStringLiteral("duplicate"), false},
            {QStringLiteral("existing"), QVariantList{}},
        };

        if (!candidates.isEmpty()) {
            AppError error;
            const QString hash = ContentHasher::sha256File(entry.path, nullptr, &error);
            if (hash.isEmpty()) {
                skipped.append(QVariantMap{{QStringLiteral("path"), entry.path},
                                           {QStringLiteral("reason"), error.message}});
                continue;
            }

            item.insert(QStringLiteral("hashed"), true);
            item.insert(QStringLiteral("contentHash"), hash);

            QVariantList existing;
            for (const QVariantMap &row : candidates) {
                if (row.value(QStringLiteral("content_hash")).toString().compare(
                        hash, Qt::CaseInsensitive)
                    != 0) {
                    continue;
                }
                existing.append(QVariantMap{
                    {QStringLiteral("fileId"), row.value(QStringLiteral("file_id"))},
                    {QStringLiteral("title"), row.value(QStringLiteral("title"))},
                    {QStringLiteral("typeName"), row.value(QStringLiteral("type_name"))},
                    {QStringLiteral("relativePath"), row.value(QStringLiteral("relative_path"))},
                });
            }

            if (!existing.isEmpty()) {
                item.insert(QStringLiteral("duplicate"), true);
                item.insert(QStringLiteral("existing"), existing);
                hasDuplicates = true;
            }
        }

        checked.append(item);
    }

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("checked"), checked.size()},
        {QStringLiteral("files"), checked},
        {QStringLiteral("skipped"), skipped},
        {QStringLiteral("hasDuplicates"), hasDuplicates},
    };
}

int DedupService::indexPackage(qint64 fileId, const QVariantList &entries)
{
    if (m_library == nullptr || !m_library->isOpen()) {
        qWarning().noquote() << "[dedup] 写入指纹失败：没有打开的库";
        return -1;
    }
    if (fileId <= 0 || entries.isEmpty())
        return 0;

    DbAccess &db = m_library->dbAccess();
    AppError error;
    if (!db.transaction(&error)) {
        qWarning().noquote() << "[dedup] 开启指纹事务失败：" << error.message;
        return -1;
    }

    int written = 0;
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString relativePath = entry.value(QStringLiteral("relativePath")).toString();
        const QString sourcePath = entry.value(QStringLiteral("sourcePath")).toString();
        if (relativePath.isEmpty() || sourcePath.isEmpty())
            continue;
        if (!MediaTypes::isImageFile(sourcePath))
            continue;

        // 预检已算过哈希/大小的候选直接复用，避免导入时二次读盘
        const qint64 cachedSize = entry.value(QStringLiteral("size")).toLongLong();
        const qint64 sizeBytes = cachedSize > 0 ? cachedSize : QFileInfo(sourcePath).size();
        QString hash = entry.value(QStringLiteral("contentHash")).toString();
        if (hash.isEmpty()) {
            AppError hashError;
            hash = ContentHasher::sha256File(sourcePath, nullptr, &hashError);
            if (hash.isEmpty()) {
                qWarning().noquote() << "[dedup] 内容哈希计算失败，保留空指纹行："
                                     << sourcePath << hashError.message;
            }
        }

        // 感知指纹在导入时一次算齐；失败时留空，由「补指纹」入口修复
        AppError imageError;
        const ImageFingerprint fingerprint = ImageHasher::compute(sourcePath, &imageError);
        if (!fingerprint.isValid()) {
            qWarning().noquote() << "[dedup] 感知指纹计算失败，保留空列："
                                 << sourcePath << imageError.message;
        }

        const QVariant contentHash = hash.isEmpty() ? QVariant() : QVariant(hash);
        const QVariant ahash =
            fingerprint.ahash.isEmpty() ? QVariant() : QVariant(fingerprint.ahash);
        const QVariant dhash =
            fingerprint.dhash.isEmpty() ? QVariant() : QVariant(fingerprint.dhash);
        const QVariant phash =
            fingerprint.phash.isEmpty() ? QVariant() : QVariant(fingerprint.phash);

        if (!db.execute(
                QStringLiteral("INSERT OR REPLACE INTO photohash "
                               "(id, relative_path, content_hash, size_bytes, "
                               " ahash, phash, dhash, algo_version) "
                               "VALUES (:id, :relative_path, :content_hash, :size_bytes, "
                               "        :ahash, :phash, :dhash, :algo_version)"),
                {
                    {QStringLiteral("id"), fileId},
                    {QStringLiteral("relative_path"), relativePath},
                    {QStringLiteral("content_hash"), contentHash},
                    {QStringLiteral("size_bytes"), sizeBytes},
                    {QStringLiteral("ahash"), ahash},
                    {QStringLiteral("phash"), phash},
                    {QStringLiteral("dhash"), dhash},
                    {QStringLiteral("algo_version"), kFingerprintAlgoVersion},
                },
                &error)) {
            qWarning().noquote() << "[dedup] 写入指纹行失败：" << error.message;
            db.rollback();
            return -1;
        }
        ++written;
    }

    if (!db.commit(&error)) {
        qWarning().noquote() << "[dedup] 提交指纹事务失败：" << error.message;
        db.rollback();
        return -1;
    }

    qInfo().noquote() << "[dedup] 已写入内容与感知指纹：fileId =" << fileId
                      << "行数 =" << written;
    return written;
}

QVariantMap DedupService::fingerprintStatus(const QVariantMap &query)
{
    if (m_library == nullptr || !m_library->isOpen())
        return fail(QStringLiteral("没有打开的库"));

    QString clause;
    QVariantMap params;
    buildTypeFilter(query, &clause, &params);

    const QString countSql = QStringLiteral(
        "SELECT COUNT(*) AS total_rows, "
        "SUM(CASE WHEN p.content_hash IS NULL OR p.content_hash = '' THEN 1 ELSE 0 END) "
        "  AS missing_content, "
        "SUM(CASE WHEN p.phash IS NULL OR p.phash = '' THEN 1 ELSE 0 END) AS missing_perceptual, "
        "SUM(CASE WHEN (p.content_hash IS NULL OR p.content_hash = '') "
        "              OR (p.phash IS NULL OR p.phash = '') THEN 1 ELSE 0 END) AS missing_any "
        "FROM photohash p "
        "JOIN file f ON f.id = p.id "
        "JOIN type t ON t.id = f.type_id "
        "WHERE 1 = 1 %1")
        .arg(clause);

    AppError error;
    QVariantMap counts;
    if (!m_library->dbAccess().selectOne(countSql, params, counts, &error))
        return fail(error.message);

    const QString packageSql = QStringLiteral(
        "SELECT COUNT(*) AS packages_without_rows "
        "FROM file f JOIN type t ON t.id = f.type_id "
        "WHERE 1 = 1 %1 "
        "AND NOT EXISTS (SELECT 1 FROM photohash p WHERE p.id = f.id)")
        .arg(clause);

    QVariantMap packageCounts;
    if (!m_library->dbAccess().selectOne(packageSql, params, packageCounts, &error))
        return fail(error.message);

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("totalRows"), counts.value(QStringLiteral("total_rows")).toInt()},
        {QStringLiteral("missingContent"),
         counts.value(QStringLiteral("missing_content")).toInt()},
        {QStringLiteral("missingPerceptual"),
         counts.value(QStringLiteral("missing_perceptual")).toInt()},
        {QStringLiteral("missingAny"), counts.value(QStringLiteral("missing_any")).toInt()},
        {QStringLiteral("packagesWithoutRows"),
         packageCounts.value(QStringLiteral("packages_without_rows")).toInt()},
    };
}

QVariantMap DedupService::startScan(const QVariantMap &query)
{
    if (m_library == nullptr || !m_library->isOpen())
        return fail(QStringLiteral("没有打开的库，无法查重"));
    if (m_busy)
        return fail(QStringLiteral("已有查重或补指纹任务在进行"));

    waitForPreviousThread();

    QVector<DedupScanItem> items;
    QString message;
    if (!collectScanItems(query, &items, &message))
        return fail(message);

    const int threshold = qBound(0, query.value(QStringLiteral("threshold"), 0).toInt(), 16);
    const QString externalFolder =
        query.value(QStringLiteral("externalFolder")).toString().trimmed();
    const bool recursive = query.value(QStringLiteral("recursive"), true).toBool();
    const int libraryFiles = static_cast<int>(items.size());

    auto *worker =
        new DedupScanWorker(std::move(items), externalFolder, recursive, threshold);
    auto *thread = new QThread();
    worker->moveToThread(thread);

    const int generation = ++m_generation;

    connect(worker, &DedupScanWorker::progress, this, &DedupService::scanProgress);
    connect(worker, &DedupScanWorker::finished, this,
            [this, generation](const QVariantMap &summary, const QVariantList &rows) {
                if (generation != m_generation)
                    return; // 上一任务的残留回调：已开新任务，丢弃
                m_busy = false;
                m_scanWorker = nullptr;
                emit scanFinished(summary, rows);
            });
    // 直接连接：worker 在主线程之外发结束信号，若排队到主线程，主线程 wait() 时会卡死
    connect(worker, &DedupScanWorker::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::started, worker, &DedupScanWorker::run);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread)
            m_thread.clear();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater, Qt::DirectConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    m_busy = true;
    m_thread = thread;
    m_scanWorker = worker;
    thread->start();

    qInfo().noquote() << "[dedup] 启动扫描：库内" << libraryFiles << "阈值" << threshold
                      << "外部目录" << externalFolder;
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("libraryFiles"), libraryFiles},
        {QStringLiteral("threshold"), threshold},
    };
}

QVariantMap DedupService::startBackfill(const QVariantMap &query)
{
    if (m_library == nullptr || !m_library->isOpen())
        return fail(QStringLiteral("没有打开的库，无法补指纹"));
    if (m_busy)
        return fail(QStringLiteral("已有查重或补指纹任务在进行"));

    waitForPreviousThread();

    QVector<DedupBackfillItem> items;
    int warningCount = 0;
    const int count = collectBackfillItems(query, &items, &warningCount);
    if (count < 0)
        return fail(QStringLiteral("读取待补指纹记录失败"));
    if (items.isEmpty()) {
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("nothing"), true},
            {QStringLiteral("message"), QStringLiteral("没有需要补指纹的文件")},
        };
    }

    auto *worker = new DedupBackfillWorker(std::move(items));
    auto *thread = new QThread();
    worker->moveToThread(thread);

    // 记录任务发起时的库：若中途切库/关库，禁止把旧库指纹写入新库连接
    const QString startedLibrary = m_library->currentName();
    const int generation = ++m_generation;

    connect(worker, &DedupBackfillWorker::progress, this, &DedupService::backfillProgress);
    connect(worker, &DedupBackfillWorker::finished, this,
            [this, startedLibrary, generation](const QVariantMap &summary,
                                               const QVariantList &updates) {
                if (generation != m_generation)
                    return; // 上一任务的残留回调：已开新任务，丢弃
                QVariantMap finalSummary = summary;
                int written = 0;
                const bool sameLibrary = m_library != nullptr && m_library->isOpen()
                    && m_library->currentName() == startedLibrary;
                if (!updates.isEmpty()) {
                    if (!sameLibrary) {
                        finalSummary.insert(QStringLiteral("ok"), false);
                        finalSummary.insert(QStringLiteral("message"),
                                            QStringLiteral("库已切换或关闭，补指纹结果未写入"));
                    } else {
                        written = writeFingerprintUpdates(updates);
                        if (written < 0) {
                            finalSummary.insert(QStringLiteral("ok"), false);
                            finalSummary.insert(QStringLiteral("message"),
                                                QStringLiteral("指纹写入数据库失败"));
                        }
                    }
                }
                finalSummary.insert(QStringLiteral("written"), written);
                m_busy = false;
                m_backfillWorker = nullptr;
                emit backfillFinished(finalSummary);
            });
    connect(worker, &DedupBackfillWorker::finished, thread, &QThread::quit,
            Qt::DirectConnection);
    connect(thread, &QThread::started, worker, &DedupBackfillWorker::run);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread)
            m_thread.clear();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    m_busy = true;
    m_thread = thread;
    m_backfillWorker = worker;
    thread->start();

    qInfo().noquote() << "[dedup] 启动补指纹：条目" << count << "跳过" << warningCount;
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("total"), count},
        {QStringLiteral("skipped"), warningCount},
    };
}

void DedupService::cancel()
{
    cancelCurrent();
}

void DedupService::clearSession()
{
    // 外部文件指纹只存在于工作线程对象内；请求取消后随对象释放，库内指纹不受影响
    cancelCurrent();
}

void DedupService::buildTypeFilter(const QVariantMap &query, QString *clause,
                                   QVariantMap *params)
{
    clause->clear();
    const bool allTypes = query.value(QStringLiteral("allTypes"), true).toBool();
    const QStringList forms = formsFromQuery(query);
    if (allTypes || forms.isEmpty())
        return; // 全部类型：不加过滤

    QStringList placeholders;
    for (int index = 0; index < forms.size(); ++index) {
        const QString key = QStringLiteral("form%1").arg(index);
        placeholders.append(QStringLiteral(":") + key);
        params->insert(key, forms.at(index));
    }
    *clause = QStringLiteral(" AND t.form IN (%1)").arg(placeholders.join(QLatin1Char(',')));
}

bool DedupService::collectScanItems(const QVariantMap &query, QVector<DedupScanItem> *items,
                                    QString *message) const
{
    QString clause;
    QVariantMap params;
    buildTypeFilter(query, &clause, &params);

    const QString sql = QStringLiteral(
        "SELECT p.id AS file_id, p.relative_path, p.content_hash, p.size_bytes, "
        "       p.ahash, p.phash, p.dhash, f.storage_path, f.title, t.name AS type_name, "
        "       (SELECT c.name FROM file_creator fc "
        "          JOIN creator c ON c.id = fc.creator_id "
        "         WHERE fc.file_id = f.id "
        "         ORDER BY fc.sort_order, fc.creator_id LIMIT 1) AS creator_name "
        "FROM photohash p "
        "JOIN file f ON f.id = p.id "
        "JOIN type t ON t.id = f.type_id "
        "WHERE 1 = 1 %1 "
        "ORDER BY p.id, p.relative_path")
        .arg(clause);

    AppError error;
    const QList<QVariantMap> rows = m_library->dbAccess().select(sql, params, &error);
    if (!error.ok()) {
        *message = error.message;
        return false;
    }

    const QString libraryName = m_library->currentName();
    const QString linkAddress = m_library->config().entry(libraryName).linkAddress;

    for (const QVariantMap &row : rows) {
        const QString relativePath = row.value(QStringLiteral("relative_path")).toString();
        if (!MediaTypes::isImageFile(relativePath))
            continue;

        const qint64 fileId = row.value(QStringLiteral("file_id")).toLongLong();
        const int bucket = row.value(QStringLiteral("storage_path")).toInt();
        DedupScanItem item;
        item.fileId = fileId;
        item.relativePath = relativePath;
        item.absolutePath =
            StorageService::packageFilePath(linkAddress, bucket, fileId, relativePath);
        item.title = row.value(QStringLiteral("title")).toString();
        item.typeName = row.value(QStringLiteral("type_name")).toString();
        item.creatorName = row.value(QStringLiteral("creator_name")).toString();
        item.size = row.value(QStringLiteral("size_bytes")).toLongLong();
        if (item.size <= 0)
            item.size = QFileInfo(item.absolutePath).size();
        item.contentHash = row.value(QStringLiteral("content_hash")).toString();
        item.ahash = row.value(QStringLiteral("ahash")).toString();
        item.dhash = row.value(QStringLiteral("dhash")).toString();
        item.phash = row.value(QStringLiteral("phash")).toString();
        item.external = false;
        items->append(item);
    }
    return true;
}

int DedupService::collectBackfillItems(const QVariantMap &query,
                                       QVector<DedupBackfillItem> *items,
                                       int *warningCount) const
{
    QString clause;
    QVariantMap params;
    buildTypeFilter(query, &clause, &params);

    const QString libraryName = m_library->currentName();
    const QString linkAddress = m_library->config().entry(libraryName).linkAddress;

    // 1) 已有 photohash 行，但缺 content_hash 或感知指纹
    const QString missingSql = QStringLiteral(
        "SELECT p.id AS file_id, p.relative_path, p.content_hash, p.phash, "
        "       p.size_bytes, f.storage_path "
        "FROM photohash p "
        "JOIN file f ON f.id = p.id "
        "JOIN type t ON t.id = f.type_id "
        "WHERE 1 = 1 %1 "
        "AND ((p.content_hash IS NULL OR p.content_hash = '') "
        "     OR (p.phash IS NULL OR p.phash = '')) "
        "ORDER BY p.id, p.relative_path")
        .arg(clause);

    AppError error;
    const QList<QVariantMap> missingRows =
        m_library->dbAccess().select(missingSql, params, &error);
    if (!error.ok()) {
        qWarning().noquote() << "[dedup] 查询待补指纹行失败：" << error.message;
        return -1;
    }

    for (const QVariantMap &row : missingRows) {
        const QString relativePath = row.value(QStringLiteral("relative_path")).toString();
        if (!MediaTypes::isImageFile(relativePath))
            continue;

        const bool missingContent = row.value(QStringLiteral("content_hash")).toString().isEmpty();
        const bool missingPerceptual = row.value(QStringLiteral("phash")).toString().isEmpty();
        if (!missingContent && !missingPerceptual)
            continue;

        const qint64 fileId = row.value(QStringLiteral("file_id")).toLongLong();
        const int bucket = row.value(QStringLiteral("storage_path")).toInt();
        DedupBackfillItem item;
        item.fileId = fileId;
        item.relativePath = relativePath;
        item.absolutePath =
            StorageService::packageFilePath(linkAddress, bucket, fileId, relativePath);
        item.size = row.value(QStringLiteral("size_bytes")).toLongLong();
        item.needContent = missingContent;
        item.needPerceptual = missingPerceptual;
        items->append(item);
    }

    // 2) 完全没有 photohash 行的旧包：按包目录内实际图片建行
    const QString noRowsSql = QStringLiteral(
        "SELECT f.id AS file_id, f.storage_path "
        "FROM file f JOIN type t ON t.id = f.type_id "
        "WHERE 1 = 1 %1 "
        "AND NOT EXISTS (SELECT 1 FROM photohash p WHERE p.id = f.id) "
        "ORDER BY f.id")
        .arg(clause);

    const QList<QVariantMap> packages =
        m_library->dbAccess().select(noRowsSql, params, &error);
    if (!error.ok()) {
        qWarning().noquote() << "[dedup] 查询未建指纹的包失败：" << error.message;
        return -1;
    }

    for (const QVariantMap &row : packages) {
        const qint64 fileId = row.value(QStringLiteral("file_id")).toLongLong();
        const int bucket = row.value(QStringLiteral("storage_path")).toInt();
        const QString dirPath = StorageService::packageDir(linkAddress, bucket, fileId);
        const QDir dir(dirPath);
        if (!dir.exists()) {
            ++(*warningCount);
            qWarning().noquote() << "[dedup] 包目录缺失，跳过补指纹：" << dirPath;
            continue;
        }

        const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &info : files) {
            if (!MediaTypes::isImageFile(info.fileName()))
                continue;
            DedupBackfillItem item;
            item.fileId = fileId;
            item.relativePath = info.fileName();
            item.absolutePath = info.absoluteFilePath();
            item.size = info.size();
            item.needContent = true;
            item.needPerceptual = true;
            items->append(item);
        }
    }

    return static_cast<int>(items->size());
}

int DedupService::writeFingerprintUpdates(const QVariantList &updates)
{
    if (updates.isEmpty())
        return 0;
    if (m_library == nullptr || !m_library->isOpen())
        return -1;

    DbAccess &db = m_library->dbAccess();
    AppError error;
    if (!db.transaction(&error)) {
        qWarning().noquote() << "[dedup] 补指纹写入事务失败：" << error.message;
        return -1;
    }

    int written = 0;
    for (const QVariant &value : updates) {
        const QVariantMap update = value.toMap();
        const QString contentHash = update.value(QStringLiteral("contentHash")).toString();
        const QString ahash = update.value(QStringLiteral("ahash")).toString();
        const QString dhash = update.value(QStringLiteral("dhash")).toString();
        const QString phash = update.value(QStringLiteral("phash")).toString();

        // COALESCE：只覆盖本次算出的列，未重算的列保持原值
        if (!db.execute(
                QStringLiteral(
                    "INSERT INTO photohash "
                    "(id, relative_path, content_hash, size_bytes, ahash, phash, dhash, "
                    " algo_version) "
                    "VALUES (:id, :relative_path, :content_hash, :size_bytes, :ahash, :phash, "
                    "        :dhash, :algo_version) "
                    "ON CONFLICT(id, relative_path) DO UPDATE SET "
                    "content_hash = COALESCE(excluded.content_hash, content_hash), "
                    "size_bytes   = excluded.size_bytes, "
                    "ahash        = COALESCE(excluded.ahash, ahash), "
                    "phash        = COALESCE(excluded.phash, phash), "
                    "dhash        = COALESCE(excluded.dhash, dhash), "
                    "algo_version = excluded.algo_version"),
                {
                    {QStringLiteral("id"), update.value(QStringLiteral("fileId"))},
                    {QStringLiteral("relative_path"),
                     update.value(QStringLiteral("relativePath"))},
                    {QStringLiteral("content_hash"),
                     contentHash.isEmpty() ? QVariant() : QVariant(contentHash)},
                    {QStringLiteral("size_bytes"), update.value(QStringLiteral("size"))},
                    {QStringLiteral("ahash"), ahash.isEmpty() ? QVariant() : QVariant(ahash)},
                    {QStringLiteral("phash"), phash.isEmpty() ? QVariant() : QVariant(phash)},
                    {QStringLiteral("dhash"), dhash.isEmpty() ? QVariant() : QVariant(dhash)},
                    {QStringLiteral("algo_version"), kFingerprintAlgoVersion},
                },
                &error)) {
            qWarning().noquote() << "[dedup] 补指纹写入失败：" << error.message;
            db.rollback();
            return -1;
        }
        ++written;
    }

    if (!db.commit(&error)) {
        qWarning().noquote() << "[dedup] 补指纹提交失败：" << error.message;
        db.rollback();
        return -1;
    }
    return written;
}

void DedupService::waitForPreviousThread()
{
    if (m_thread == nullptr)
        return;
    // 上一任务已结束但线程尚未回收：等待其退出，避免覆盖指针
    m_thread->quit();
    m_thread->wait();
    m_thread.clear();
}

void DedupService::cancelCurrent()
{
    if (m_scanWorker)
        m_scanWorker->requestCancel();
    if (m_backfillWorker)
        m_backfillWorker->requestCancel();
}

QVariantMap DedupService::fail(const QString &message) const
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), message},
    };
}
