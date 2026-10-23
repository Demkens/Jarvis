#include "DedupScanWorker.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include "common/AppError.h"
#include "common/MediaTypes.h"
#include "dedup/ContentHasher.h"
#include "dedup/ImageHasher.h"

namespace {

int popCount(quint64 value)
{
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

// 路径比较键：Windows 下大小写不敏感，避免同一文件因盘符/大小写差异被当成两个来源
QString pathKey(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(path).toLower();
#else
    return QDir::cleanPath(path);
#endif
}

// 来源标识：库内同一数据包算一个来源（差分图集的刻意重复帧不单独计入结果），
// 外部每个文件各自算一个来源。
QString sourceKey(const DedupScanItem &item)
{
    if (item.external)
        return QStringLiteral("ext:") + pathKey(item.absolutePath);
    return QStringLiteral("lib:") + QString::number(item.fileId);
}

// 感知指纹索引（BK-tree）：64 位汉明距离空间内做「距离 ≤ 半径」的精确候选检索，
// 避免全量两两比较；构建与查询都只发生在本工作线程内。
class HammingIndex
{
public:
    // 往索引里加入一个指纹；item 为调用方自定义的下标（此处是簇编号）。
    void add(quint64 key, int item)
    {
        if (m_root == nullptr) {
            m_root = std::make_unique<Node>();
            m_root->key = key;
            m_root->item = item;
            return;
        }

        Node *node = m_root.get();
        forever {
            const int distance = popCount(key ^ node->key);
            const auto child = node->children.constFind(distance);
            if (child != node->children.constEnd()) {
                node = child.value();
                continue;
            }
            auto *created = new Node();
            created->key = key;
            created->item = item;
            node->children.insert(distance, created);
            return;
        }
    }

    // 查询与 key 汉明距离 ≤ radius 的候选项下标，追加到 out。
    void query(quint64 key, int radius, QVector<int> &out) const
    {
        if (m_root != nullptr)
            queryNode(m_root.get(), key, radius, out);
    }

private:
    struct Node
    {
        quint64 key = 0;
        int item = -1;
        QHash<int, Node *> children;

        ~Node()
        {
            for (Node *child : children)
                delete child;
        }
    };

    static void queryNode(const Node *node, quint64 key, int radius, QVector<int> &out)
    {
        const int distance = popCount(key ^ node->key);
        if (distance <= radius)
            out.append(node->item);

        // 子节点按与父节点的距离分桶：只有距离落在 [d-r, d+r] 的分支才可能命中
        const int from = distance - radius;
        const int to = distance + radius;
        for (auto it = node->children.constBegin(); it != node->children.constEnd(); ++it) {
            if (it.key() >= from && it.key() <= to)
                queryNode(it.value(), key, radius, out);
        }
    }

    std::unique_ptr<Node> m_root;
};

// 并查集：把精确重复与近似重复合并为同一组。
class UnionFind
{
public:
    explicit UnionFind(int count)
        : m_parent(count)
    {
        for (int index = 0; index < count; ++index)
            m_parent[index] = index;
    }

    int find(int value)
    {
        int root = value;
        while (m_parent.at(root) != root)
            root = m_parent.at(root);
        while (m_parent.at(value) != value) {
            const int next = m_parent.at(value);
            m_parent[value] = root;
            value = next;
        }
        return root;
    }

    void unite(int a, int b)
    {
        const int rootA = find(a);
        const int rootB = find(b);
        if (rootA != rootB)
            m_parent[rootB] = rootA;
    }

private:
    QVector<int> m_parent;
};

// 解析 16 位十六进制指纹；非法返回 false。
bool parseFingerprint(const QString &hex, quint64 *value)
{
    if (value == nullptr || hex.size() != 16)
        return false;
    bool ok = false;
    const quint64 parsed = hex.toULongLong(&ok, 16);
    if (!ok)
        return false;
    *value = parsed;
    return true;
}

} // namespace

DedupScanWorker::DedupScanWorker(QVector<DedupScanItem> items, const QString &externalFolder,
                                 bool recursive, int threshold, QObject *parent)
    : QObject(parent)
    , m_items(std::move(items))
    , m_externalFolder(externalFolder)
    , m_recursive(recursive)
    , m_threshold(threshold)
{
}

void DedupScanWorker::requestCancel()
{
    m_canceled.store(true);
}

void DedupScanWorker::run()
{
    QElapsedTimer timer;
    timer.start();

    const int libraryFiles = static_cast<int>(m_items.size());
    QStringList warnings;
    int skippedFiles = 0;
    int failedFiles = 0;

    // 库内文件绝对路径集合：外部文件夹若包含库内实体目录，跳过这些文件，避免同图双计
    QSet<QString> knownPaths;
    for (const DedupScanItem &item : m_items)
        knownPaths.insert(pathKey(item.absolutePath));

    // ---------- 阶段一：外部附件预处理（枚举 + 计算指纹） ----------
    QStringList externalFiles;
    if (!m_externalFolder.isEmpty()) {
        const QDir root(m_externalFolder);
        if (!root.exists()) {
            warnings.append(QStringLiteral("附件文件夹不存在：%1")
                                .arg(QDir::toNativeSeparators(m_externalFolder)));
        } else {
            const QDirIterator::IteratorFlags flags =
                m_recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
            QDirIterator iterator(root.absolutePath(), QDir::Files, flags);
            while (iterator.hasNext()) {
                const QFileInfo info(iterator.next());
                if (!MediaTypes::isImageFile(info.fileName())) {
                    ++skippedFiles; // 视频等非图片本期不参与图片查重
                    continue;
                }
                if (knownPaths.contains(pathKey(info.absoluteFilePath())))
                    continue;
                externalFiles.append(info.absoluteFilePath());
            }
            std::sort(externalFiles.begin(), externalFiles.end(),
                      [](const QString &left, const QString &right) {
                          return left.compare(right, Qt::CaseInsensitive) < 0;
                      });
        }
    }

    const int prepareTotal = static_cast<int>(externalFiles.size());
    const int prepareStep = qMax(1, prepareTotal / 200);
    emit progress(QStringLiteral("prepare"), 0, prepareTotal);

    int externalFilesAdded = 0;

    // 取消时的统一收尾：只回统计，不带结果行
    auto finishCanceled = [this, libraryFiles, &externalFilesAdded]() {
        emit finished(
            QVariantMap{
                {QStringLiteral("ok"), true},
                {QStringLiteral("canceled"), true},
                {QStringLiteral("mode"),
                 m_threshold == 0 ? QStringLiteral("exact") : QStringLiteral("near")},
                {QStringLiteral("threshold"), m_threshold},
                {QStringLiteral("libraryFiles"), libraryFiles},
                {QStringLiteral("externalFiles"), externalFilesAdded},
                {QStringLiteral("groups"), 0},
                {QStringLiteral("files"), 0},
            },
            QVariantList{});
    };

    for (int index = 0; index < externalFiles.size(); ++index) {
        if (m_canceled.load())
            break;

        const QString path = externalFiles.at(index);
        const QFileInfo info(path);
        DedupScanItem item;
        item.external = true;
        item.absolutePath = info.absoluteFilePath();
        item.title = info.fileName();
        item.size = info.size();

        AppError hashError;
        item.contentHash = ContentHasher::sha256File(path, &item.size, &hashError);
        if (item.contentHash.isEmpty() && warnings.size() < 30) {
            warnings.append(QStringLiteral("内容哈希失败：%1（%2）")
                                .arg(item.title, hashError.message));
        }

        // 精确模式只用 content_hash；感知指纹只在近似模式需要，避免无谓解码
        if (m_threshold > 0) {
            AppError imageError;
            const ImageFingerprint fingerprint = ImageHasher::compute(path, &imageError);
            item.ahash = fingerprint.ahash;
            item.dhash = fingerprint.dhash;
            item.phash = fingerprint.phash;
            if (!fingerprint.isValid() && warnings.size() < 30) {
                warnings.append(QStringLiteral("感知指纹失败：%1（%2）")
                                    .arg(item.title, imageError.message));
            }
        }

        if (item.contentHash.isEmpty() && item.phash.isEmpty()) {
            ++failedFiles; // 两种指纹都算不出来，无法参与任何查重
        } else {
            m_items.append(item);
            ++externalFilesAdded;
        }

        // 每 1/200 或最后一个文件发一次进度，避免上万文件时刷爆信号队列
        if (index % prepareStep == 0 || index + 1 == prepareTotal)
            emit progress(QStringLiteral("prepare"), index + 1, prepareTotal);
    }

    if (m_canceled.load()) {
        finishCanceled();
        return;
    }

    // ---------- 阶段二：分组 ----------
    // 1) 完全重复：content_hash 等价类作为不可拆分的「单元」
    UnionFind exactGroups(m_items.size());
    QHash<QString, int> exactFirst;
    for (int index = 0; index < m_items.size(); ++index) {
        const QString &hash = m_items.at(index).contentHash;
        if (hash.isEmpty())
            continue;
        const auto found = exactFirst.constFind(hash);
        if (found == exactFirst.constEnd())
            exactFirst.insert(hash, index);
        else
            exactGroups.unite(found.value(), index);
    }

    QVector<QVector<int>> units;
    QHash<int, int> unitOfRoot;
    for (int index = 0; index < m_items.size(); ++index) {
        const int root = exactGroups.find(index);
        const auto found = unitOfRoot.find(root);
        if (found == unitOfRoot.end()) {
            unitOfRoot.insert(root, static_cast<int>(units.size()));
            units.append(QVector<int>{index});
        } else {
            units[found.value()].append(index);
        }
    }

    // 2) 近似聚类（代表聚类，不做传递闭包）：
    //    每个单元只与「已存在的簇代表」比较，命中则并入最近的簇，否则自成一簇；
    //    避免一段动画的相邻帧通过链式相似把整段都并进同一组。
    QVector<int> unitCluster(units.size(), -1);
    QVector<int> clusterRepresentative;
    QVector<quint64> clusterKeys;
    HammingIndex clusterIndex;

    const int compareTotal = static_cast<int>(units.size());
    const int progressStep = qMax(1, compareTotal / 200);
    emit progress(QStringLiteral("compare"), 0, compareTotal);

    for (int unit = 0; unit < units.size(); ++unit) {
        if (m_canceled.load())
            break;

        // 单元代表：优先取第一个有合法 pHash 的项；没有则取首项（只能靠 content_hash 成组）
        int representative = units.at(unit).first();
        quint64 key = 0;
        bool hasKey = false;
        if (m_threshold > 0) {
            for (int index : units.at(unit)) {
                if (parseFingerprint(m_items.at(index).phash, &key)) {
                    representative = index;
                    hasKey = true;
                    break;
                }
            }
        }

        int cluster = -1;
        if (hasKey) {
            QVector<int> candidates;
            clusterIndex.query(key, m_threshold, candidates);
            int bestDistance = std::numeric_limits<int>::max();
            for (int candidate : candidates) {
                const int distance = popCount(key ^ clusterKeys.at(candidate));
                if (distance < bestDistance) {
                    bestDistance = distance;
                    cluster = candidate;
                }
            }
        }

        if (cluster < 0) {
            cluster = static_cast<int>(clusterRepresentative.size());
            clusterRepresentative.append(representative);
            clusterKeys.append(hasKey ? key : quint64(0));
            if (hasKey)
                clusterIndex.add(key, cluster);
        }
        unitCluster[unit] = cluster;

        if (unit % progressStep == 0 || unit + 1 == compareTotal)
            emit progress(QStringLiteral("compare"), unit + 1, compareTotal);
    }

    if (m_canceled.load()) {
        finishCanceled();
        return;
    }

    // 3) 簇 → 组成员。只保留「跨来源」的组：
    //    同一数据包内部的重复/相似帧不列为查重结果（差分图集可能刻意重复）。
    struct Group
    {
        QVector<int> members;
        int representative = -1;
    };

    QHash<int, Group> groupOfCluster;
    for (int unit = 0; unit < units.size(); ++unit) {
        const int cluster = unitCluster.at(unit);
        Group &group = groupOfCluster[cluster];
        if (group.representative < 0)
            group.representative = clusterRepresentative.at(cluster);
        for (int index : units.at(unit))
            group.members.append(index);
    }

    QVector<Group> duplicateGroups;
    for (auto it = groupOfCluster.constBegin(); it != groupOfCluster.constEnd(); ++it) {
        if (it.value().members.size() < 2)
            continue;

        QSet<QString> sources;
        for (int index : it.value().members)
            sources.insert(sourceKey(m_items.at(index)));
        if (sources.size() < 2)
            continue; // 仅同包内部重复：不计入结果

        duplicateGroups.append(it.value());
    }

    // 组间排序：组越大越靠前；同规模按代表项标题排序，保证结果稳定
    std::sort(duplicateGroups.begin(), duplicateGroups.end(),
              [this](const Group &left, const Group &right) {
                  if (left.members.size() != right.members.size())
                      return left.members.size() > right.members.size();
                  return m_items.at(left.representative).title.compare(
                             m_items.at(right.representative).title, Qt::CaseInsensitive)
                      < 0;
              });

    QVariantList rows;
    for (int groupIndex = 0; groupIndex < duplicateGroups.size(); ++groupIndex) {
        const Group &group = duplicateGroups.at(groupIndex);
        QVector<int> members = group.members;
        const int representative = group.representative;

        // 代表项固定排在首位，其余按与代表项的距离升序
        members.removeAll(representative);
        members.prepend(representative);

        // 组内排序：代表项在前，其余按与代表项的距离升序（未知距离排最后）
        std::sort(members.begin() + 1, members.end(),
                  [this, representative](int left, int right) {
                      const int leftDistance =
                          ImageHasher::hamming(m_items.at(representative).phash,
                                               m_items.at(left).phash);
                      const int rightDistance =
                          ImageHasher::hamming(m_items.at(representative).phash,
                                               m_items.at(right).phash);
                      if (leftDistance != rightDistance) {
                          if (leftDistance < 0)
                              return false;
                          if (rightDistance < 0)
                              return true;
                          return leftDistance < rightDistance;
                      }
                      return m_items.at(left).title.compare(m_items.at(right).title,
                                                            Qt::CaseInsensitive)
                          < 0;
                  });

        const DedupScanItem &reference = m_items.at(representative);

        // 组内同内容哈希出现次数：>1 表示该文件在本组里有字节级完全相同的对手
        QHash<QString, int> hashCounts;
        for (int member : members) {
            const QString &hash = m_items.at(member).contentHash;
            if (!hash.isEmpty())
                hashCounts[hash] += 1;
        }

        for (int member : members) {
            const DedupScanItem &item = m_items.at(member);
            const bool exact = !item.contentHash.isEmpty()
                && hashCounts.value(item.contentHash) > 1;
            int distance = 0;
            int aDistance = 0;
            int dDistance = 0;
            if (member != representative) {
                distance = exact ? 0
                                 : ImageHasher::hamming(reference.phash, item.phash);
                aDistance = ImageHasher::hamming(reference.ahash, item.ahash);
                dDistance = ImageHasher::hamming(reference.dhash, item.dhash);
            }

            rows.append(QVariantMap{
                {QStringLiteral("group"), groupIndex},
                {QStringLiteral("origin"),
                 item.external ? QStringLiteral("external") : QStringLiteral("library")},
                {QStringLiteral("fileId"), item.fileId},
                {QStringLiteral("relativePath"), item.relativePath},
                {QStringLiteral("path"), item.absolutePath},
                {QStringLiteral("title"), item.title},
                {QStringLiteral("typeName"), item.typeName},
                {QStringLiteral("creatorName"), item.creatorName},
                {QStringLiteral("previewUrl"),
                 QUrl::fromLocalFile(item.absolutePath).toString()},
                {QStringLiteral("size"), item.size},
                {QStringLiteral("representative"), member == representative},
                {QStringLiteral("exact"), exact},
                {QStringLiteral("distance"), distance},
                {QStringLiteral("aDistance"), aDistance},
                {QStringLiteral("dDistance"), dDistance},
            });
        }
    }

    int missingContent = 0;
    int missingPerceptual = 0;
    for (const DedupScanItem &item : m_items) {
        if (item.contentHash.isEmpty())
            ++missingContent;
        if (item.phash.isEmpty())
            ++missingPerceptual;
    }

    QVariantList warningList;
    for (const QString &warning : warnings)
        warningList.append(warning);

    qInfo().noquote() << "[dedup] 扫描完成：库内" << libraryFiles << "外部" << externalFilesAdded
                      << "组" << duplicateGroups.size() << "结果行" << rows.size()
                      << "耗时(ms)" << timer.elapsed();

    emit finished(
        QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("canceled"), false},
            {QStringLiteral("mode"),
             m_threshold == 0 ? QStringLiteral("exact") : QStringLiteral("near")},
            {QStringLiteral("threshold"), m_threshold},
            {QStringLiteral("libraryFiles"), libraryFiles},
            {QStringLiteral("externalFiles"), externalFilesAdded},
            {QStringLiteral("scanned"), static_cast<int>(m_items.size())},
            {QStringLiteral("skippedFiles"), skippedFiles},
            {QStringLiteral("failedFiles"), failedFiles},
            {QStringLiteral("missingContent"), missingContent},
            {QStringLiteral("missingPerceptual"), missingPerceptual},
            {QStringLiteral("groups"), static_cast<int>(duplicateGroups.size())},
            {QStringLiteral("files"), static_cast<int>(rows.size())},
            {QStringLiteral("elapsedMs"), timer.elapsed()},
            {QStringLiteral("warnings"), warningList},
        },
        rows);
}
