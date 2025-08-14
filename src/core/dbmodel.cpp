#include "dbmodel.h"

DatabaseModel::DatabaseModel(QObject *parent) : QObject(parent)
{
    dbLink = QSqlDatabase::addDatabase("QSQLITE", "SharedConnection");
    currentDbName = "";
}

DatabaseModel::~DatabaseModel()
{
    //QSqlQuery::finish();
    if (dbLink.isOpen()) dbLink.close();
    QSqlDatabase::removeDatabase("SharedConnection");
}

bool DatabaseModel::createDatabase(const QString& directory, const QString& dbName)
{
    // 1.预检查条件和回滚准备
    if (directory.isEmpty() || dbName.isEmpty()) {
        qWarning() << "错误：数据库或链接库地址为空";
        return false;
    }

    // 检测链接库文件夹是否存在
    QFileInfo linkdbInfo(directory);
    if (!(linkdbInfo.exists() && linkdbInfo.isDir())) {
        qWarning() << "错误：链接库不存在";
        return false;
    }

    QString envsDir = QDir("D:/Program/PROJECT/Jarvis").filePath("envs");
    QString configPath = QDir(envsDir).filePath("config.json");
    QString dbDir = QDir(envsDir).filePath(dbName);

    // 检测envs文件夹，不存在便创建
    QFileInfo envsInfo(envsDir);
    if (!(envsInfo.exists() && envsInfo.isDir())) {
        qDebug() << "未检测到envs文件夹，尝试创建...";
        if (QDir().mkpath(envsDir)) {
            qDebug() << "envs文件夹创建成功";
        } else {
            qWarning() << "错误：envs文件夹创建失败：";
            return false;
        }
    }

    // 检测数据库文件夹是否同名
    QFileInfo dbInfo(dbDir);
    if (dbInfo.exists() && dbInfo.isDir()) {
        qWarning() << "错误：数据库文件夹同名";
        return false;
    }

    // 创建临时数据结构体用于回滚
    struct RollbackInfo {
        QStringList createdDirs;
        bool configModified = false;
        QByteArray originalConfig;
    } rollback;

    // 创建匿名函数方便回滚
    auto rollbackCleanup = [&]() {
        // 恢复配置文件
        if (rollback.configModified) {
            QFile configFile(configPath);
            if (configFile.open(QIODevice::WriteOnly)) {
                configFile.write(rollback.originalConfig);
            }
        }
        // 回滚数据库
        if (dbLink.isOpen()) dbLink.close();
        if (currentDbName != "") {
            dbLink.setDatabaseName(currentDbName);
            dbLink.open();
        }
        // 删除所有已创建的目录
        for (const auto& dir : rollback.createdDirs) {
            if (!QDir(dir).removeRecursively()) {
                qWarning() << "删除文件不彻底";
                QDir(dbDir).remove(dbName + ".db");
            }
        }
    };

    // 创建链接库子目录名列表
    const QStringList linkSubDirs = {
        "novel", "draw", "comic", "episode", "film",
        "sexyfilm", "modelfilm", "singlephoto", "groupphoto"
    };

    // 2.配置文件写入操作
    QFileInfo configInfo(configPath);
    if (!(configInfo.exists() && configInfo.isFile())){
        qDebug() << "未检测到config.json文件，尝试创建...";

        QJsonObject root;
        root["currentDb"] = dbName;

        QJsonArray createdDb;
        createdDb.append(dbName);
        root["createdDb"] = createdDb;

        QJsonObject settings;
        settings["linkAddress"] = directory;
        settings["creationDate"] = QDateTime::currentSecsSinceEpoch();
        root[dbName] = settings;

        QFile jsonFile(configPath);
        if (jsonFile.open(QIODevice::WriteOnly)) {
            // 创建空滚数据
            QJsonObject backroot;
            backroot["currentDb"] = "";
            QJsonArray backcreatedDb;
            backroot["createdDb"] = backcreatedDb;
            rollback.originalConfig = QJsonDocument(backroot).toJson(QJsonDocument::Indented);
            rollback.configModified = true;

            jsonFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            jsonFile.close();
            qDebug() << "config.json文件创建成功";
        } else {
            qWarning() << "错误：无法创建文件config.json";
            return false;
        }
    } else {
        qDebug() << "检测到config.json文件，尝试写入...";
        QFile jsonFile(configPath);
        if (jsonFile.open(QIODevice::ReadWrite)) {
            // 备份原始配置
            rollback.originalConfig = jsonFile.readAll();
            rollback.configModified = true;

            QJsonDocument doc = QJsonDocument::fromJson(rollback.originalConfig);
            QJsonObject root = doc.object();

            // 检测是否数据库是否重名
            QJsonArray createdDb = root["createdDb"].toArray();
            for (const QJsonValue& value : createdDb) {
                if (value.isString() && value.toString() == dbName) {
                    qWarning() << "错误：数据库配置文件中同名";
                    jsonFile.close();
                    return false;
                }
            }

            createdDb.append(dbName);
            root["createdDb"] = createdDb;
            root["currentDb"] = dbName;

            QJsonObject settings;
            settings["linkAddress"] = directory;
            settings["creationDate"] = QDateTime::currentSecsSinceEpoch();
            root[dbName] = settings;

            jsonFile.resize(0);
            doc.setObject(root);

            if (jsonFile.write(doc.toJson(QJsonDocument::Indented))) {
                qDebug() << "写入成功";
                jsonFile.close();
            } else {
                qWarning() << "错误：写入配置文件失败";
                jsonFile.close();
                return false;
            }
        } else {
            qWarning() << "错误：无法打开文件";
            return false;
        }
    }

    // 3.相关文件夹创建
    // 创建数据库目录
    rollback.createdDirs << dbDir;
    if (!QDir().mkpath(dbDir)) {
        qWarning() << "错误：数据库文件夹创建失败";
        rollbackCleanup();
        return false;
    }
    // 创建数据库和链接库相关文件夹
    for (const auto& subDir : linkSubDirs) {
        QString dirPath = QDir(directory).filePath(subDir);
        rollback.createdDirs << dirPath;
        if (!QDir().mkpath(dirPath)) {
            qWarning() << "错误：" << subDir << "文件夹创建失败";
            rollbackCleanup();
            return false;
        }
    }
    for (const auto& subDir : linkSubDirs) {
        QString dirPath = QDir(dbDir).filePath(subDir);
        if (!QDir().mkpath(dirPath)) {
            qWarning() << "错误：" << subDir << "文件夹创建失败";
            rollbackCleanup();
            return false;
        }
    }

    // 4.创建并打开数据库
    if (dbLink.isOpen()) dbLink.close();

    QString dbpath = QDir(dbDir).filePath(dbName + ".db");
    dbLink.setDatabaseName(dbpath);

    if (!dbLink.open()) {
        qWarning() << "错误：无法打开数据库";
        rollbackCleanup();
        return false;
    }

    // 5.初始化数据库表结构
    QSqlQuery query(dbLink);
    qDebug() << "启用数据库外键支持";
    if (!query.exec("PRAGMA foreign_keys = ON;")) {
        qWarning() << "外键支持启用失败";
        rollbackCleanup();
        return false;
    }

    qDebug() << "开启事务";
    if (!dbLink.transaction()) {
        qWarning() << "开启事务失败";
        rollbackCleanup();
        return false;
    }
    bool success = true;

    QString createTableSQL_file = R"(
        CREATE TABLE IF NOT EXISTS file (
           id INTEGER PRIMARY KEY AUTOINCREMENT,
           type_id INTEGER NOT NULL,
           score INTEGER NOT NULL DEFAULT 0 CHECK (score BETWEEN 0 AND 100),
           grade INTEGER NOT NULL DEFAULT 0 CHECK (grade BETWEEN 0 AND 2),
           size INTEGER NOT NULL,
           created_at TIMESTAMP DEFAULT (strftime('%s','now')),
           updated_at TIMESTAMP DEFAULT (strftime('%s','now')),
           storage_path TEXT NOT NULL,
           cover_path TEXT);
        )";
    if (!query.exec(createTableSQL_file)) {
        qWarning() << "创建file表失败:" << query.lastError().text();
        success = false;
    }

    if (success) {
        QString createTriggerSQL_file = R"(
            CREATE TRIGGER update_file_timestamp
            BEFORE UPDATE ON file
            FOR EACH ROW
            BEGIN
               UPDATE file SET updated_at = strftime('%s','now')
               WHERE id = OLD.id;
            END;
            )";
        if (!query.exec(createTriggerSQL_file)) {
            qWarning() << "创建file表触发器失败:" << query.lastError().text();
            success = false;
        }
    }

    QString createTableSQL_novel = R"(
        CREATE TABLE novel (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            author INTEGER NOT NULL DEFAULT 0,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_novel)) {
        qWarning() << "创建novel表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_draw = R"(
        CREATE TABLE draw (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            painter INTEGER NOT NULL DEFAULT 0,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_draw)) {
        qWarning() << "创建draw表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_comic = R"(
        CREATE TABLE comic (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            painter INTEGER NOT NULL DEFAULT 0,
            language TEXT NOT NULL,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_comic)) {
        qWarning() << "创建comic表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_episode = R"(
        CREATE TABLE episode (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            language TEXT NOT NULL,
            caption TEXT NOT NULL,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_episode)) {
        qWarning() << "创建episode表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_film = R"(
        CREATE TABLE film (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            language TEXT NOT NULL,
            caption TEXT NOT NULL,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_film)) {
        qWarning() << "创建film表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_sexyfilm = R"(
        CREATE TABLE sexyfilm (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            number INTEGER NOT NULL,
            language TEXT NOT NULL,
            caption TEXT NOT NULL,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_sexyfilm)) {
        qWarning() << "创建sexyfilm表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_modelfilm = R"(
        CREATE TABLE modelfilm (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            creator NOT NULL DEFAULT 0,
            language TEXT NOT NULL,
            caption TEXT NOT NULL,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_modelfilm)) {
        qWarning() << "创建modelfilm表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_singlephoto = R"(
        CREATE TABLE singlephoto (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            modelor INTEGER NOT NULL DEFAULT 0,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_singlephoto)) {
        qWarning() << "创建singlephoto表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_groupphoto = R"(
        CREATE TABLE groupphoto (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL DEFAULT 'untitled',
            groupname TEXT NOT NULL,
            modelor INTEGER NOT NULL DEFAULT 0,
            creation_date TIMESTAMP NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_groupphoto)) {
        qWarning() << "创建groupphoto表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_creator = R"(
        CREATE TABLE creator (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);
        )";
    if (!query.exec(createTableSQL_creator)) {
        qWarning() << "创建creator表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_alias = R"(
        CREATE TABLE alias (
            creator_id INTEGER NOT NULL,
            alias TEXT NOT NULL,
            PRIMARY KEY (creator_id, alias),
            FOREIGN KEY (creator_id) REFERENCES creator(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_alias)) {
        qWarning() << "创建alias表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_photohash = R"(
        CREATE TABLE photohash (
            id INTEGER NOT NULL,
            relative_path TEXT NOT NULL,
            ahash TEXT NOT NULL,
            phash TEXT NOT NULL,
            dhash TEXT NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_photohash)) {
        qWarning() << "创建photohash表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_filmhash = R"(
        CREATE TABLE filmhash (
            id INTEGER NOT NULL,
            relative_path TEXT NOT NULL,
            video_fingerprint TEXT NOT NULL,
            FOREIGN KEY (id) REFERENCES file(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_filmhash)) {
        qWarning() << "创建filmhash表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_tag = R"(
        CREATE TABLE tag (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            parent_id INTEGER,
            CHECK (parent_id IS NULL OR parent_id != id),
            FOREIGN KEY (parent_id) REFERENCES tag(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_tag)) {
        qWarning() << "创建tag表失败:" << query.lastError().text();
        success = false;
    }

    QString createSQL_tag = "CREATE UNIQUE INDEX idx_unique_tag_name ON tag (parent_id, name);";
    if (!query.exec(createSQL_tag)) {
        qWarning() << "创建tag表附属失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_file_tag = R"(
        CREATE TABLE file_tag (
            file_id INTEGER NOT NULL,
            tag_id INTEGER NOT NULL,
            PRIMARY KEY (file_id, tag_id),
            FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE CASCADE,
            FOREIGN KEY (tag_id) REFERENCES tag(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_file_tag)) {
        qWarning() << "创建file_tag表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_series = R"(
        CREATE TABLE series (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE);
        )";
    if (!query.exec(createTableSQL_series)) {
        qWarning() << "创建series表失败:" << query.lastError().text();
        success = false;
    }

    QString createTableSQL_file_series = R"(
        CREATE TABLE file_series (
            file_id INTEGER NOT NULL,
            series_id INTEGER NOT NULL,
            number INTEGER NOT NULL,
            PRIMARY KEY (file_id, series_id),
            FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE CASCADE,
            FOREIGN KEY (series_id) REFERENCES series(id) ON DELETE CASCADE);
        )";
    if (!query.exec(createTableSQL_file_series)) {
        qWarning() << "创建file_series表失败:" << query.lastError().text();
        success = false;
    }

    if (success) {
        if (!dbLink.commit()) {
            qWarning() << "提交事务失败:" << dbLink.lastError().text();
            rollbackCleanup();
            return false;
        }
        qDebug() << "数据库初始化成功!";
    } else {
        if (!dbLink.rollback()) {
            qWarning() << "回滚事务失败:" << dbLink.lastError().text();
        }
        qDebug() << "数据库初始化失败，已回滚所有操作";
        rollbackCleanup();
        return false;
    }

    // 6.所有操作成功
    setCurrentDbName(dbName);
    return true;
}

QString DatabaseModel::getCurrentDbName() const
{
    return currentDbName;
}

void DatabaseModel::setCurrentDbName(const QString &newCurrentDbName)
{
    if (currentDbName == newCurrentDbName)
        return;
    currentDbName = newCurrentDbName;
    emit currentDbNameChanged();
}
