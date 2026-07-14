// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <memory>
#include <unordered_map>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "game_info_cache.h"

namespace {

QString JoinNpCommIds(const std::vector<std::string>& ids) {
    QStringList list;
    list.reserve(static_cast<int>(ids.size()));
    for (const auto& id : ids) {
        list << QString::fromStdString(id);
    }
    return list.join(QLatin1Char(';'));
}

std::vector<std::string> SplitNpCommIds(const QString& joined) {
    std::vector<std::string> ids;
    if (joined.isEmpty()) {
        return ids;
    }
    const QStringList list = joined.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    ids.reserve(static_cast<size_t>(list.size()));
    for (const QString& id : list) {
        ids.push_back(id.toStdString());
    }
    return ids;
}

} // namespace

class GameInfoCache::Connection {
public:
    Connection(const QString& name, const std::filesystem::path& db_path) : m_name(name) {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
            LOG_ERROR(Frontend,
                      "GameInfoCache: the Qt 'QSQLITE' driver plugin isn't available, so the "
                      "game metadata cache is disabled (titles will always be re-parsed from "
                      "disk). On Linux this usually means the Qt SQLite driver package isn't "
                      "installed (e.g. 'libqt6sql6-sqlite' on Debian/Ubuntu). For a packaged "
                      "build, make sure 'sqldrivers/qsqlite[d].{{dll,so,dylib}}' was deployed "
                      "next to the other Qt plugins (dist/qtplugins/).");
            return;
        }

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);

        QString path_str;
        Common::FS::PathToQString(path_str, db_path);
        db.setDatabaseName(path_str);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));

        if (!db.open()) {
            LOG_ERROR(Frontend, "GameInfoCache: failed to open '{}': {}", path_str.toStdString(),
                      db.lastError().text().toStdString());
            return;
        }

        QSqlQuery setup(db);
        setup.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        setup.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        if (!setup.exec(
                QStringLiteral("CREATE TABLE IF NOT EXISTS game_cache ("
                               "path TEXT PRIMARY KEY,"
                               "fingerprint INTEGER NOT NULL,"
                               "serial TEXT, name TEXT, category TEXT, app_ver TEXT, sdk_ver TEXT,"
                               "fw TEXT, region TEXT, save_dir TEXT,"
                               "icon_path TEXT, pic_path TEXT, snd0_path TEXT, np_comm_ids TEXT,"
                               "size_on_disk INTEGER, size_fingerprint INTEGER,"
                               "icon_blob BLOB, icon_fingerprint INTEGER)"))) {
            LOG_ERROR(Frontend, "GameInfoCache: failed to create schema: {}",
                      setup.lastError().text().toStdString());
            return;
        }
        LOG_INFO(Frontend, "GameInfoCache: using '{}'", path_str.toStdString());
        m_valid = true;
    }

    ~Connection() {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_name);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool IsValid() const {
        return m_valid;
    }

    QSqlDatabase Db() const {
        return QSqlDatabase::database(m_name, false);
    }

private:
    QString m_name;
    bool m_valid = false;
};

GameInfoCache::GameInfoCache(std::filesystem::path db_path) : m_db_path(std::move(db_path)) {
    std::error_code ec;
    std::filesystem::create_directories(m_db_path.parent_path(), ec);
    m_connection_prefix =
        QStringLiteral("shadLauncher4_game_info_cache_%1_").arg(reinterpret_cast<quintptr>(this));
}

GameInfoCache::~GameInfoCache() = default;

GameInfoCache::Connection& GameInfoCache::ThreadConnection() {
    thread_local std::unordered_map<const GameInfoCache*, std::unique_ptr<Connection>> connections;

    auto it = connections.find(this);
    if (it == connections.end()) {
        static std::atomic<u64> counter{0};
        const QString name =
            m_connection_prefix + QString::number(counter.fetch_add(1, std::memory_order_relaxed));
        it = connections.emplace(this, std::make_unique<Connection>(name, m_db_path)).first;
    }
    return *it->second;
}

void GameInfoCache::WarmUp() {
    ThreadConnection();
}

std::optional<GameInfo> GameInfoCache::Get(const std::string& game_path, s64 fingerprint) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return std::nullopt;
    }

    QSqlQuery query(conn.Db());
    query.prepare(QStringLiteral(
        "SELECT fingerprint, serial, name, category, app_ver, sdk_ver, fw, region, save_dir,"
        " icon_path, pic_path, snd0_path, np_comm_ids FROM game_cache WHERE path = ?"));
    query.addBindValue(QString::fromStdString(game_path));

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    if (query.value(0).toLongLong() != fingerprint) {
        return std::nullopt;
    }

    GameInfo info;
    info.path = game_path;
    info.serial = query.value(1).toString().toStdString();
    info.name = query.value(2).toString().toStdString();
    info.category = query.value(3).toString().toStdString();
    info.app_ver = query.value(4).toString().toStdString();
    info.sdk_ver = query.value(5).toString().toStdString();
    info.fw = query.value(6).toString().toStdString();
    info.region = query.value(7).toString().toStdString();
    info.save_dir = query.value(8).toString().toStdString();
    info.icon_path = query.value(9).toString().toStdString();
    info.pic_path = query.value(10).toString().toStdString();
    info.snd0_path = query.value(11).toString().toStdString();
    info.np_comm_ids = SplitNpCommIds(query.value(12).toString());
    return info;
}

std::optional<u64> GameInfoCache::GetSize(const std::string& game_path, s64 size_fingerprint) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return std::nullopt;
    }

    QSqlQuery query(conn.Db());
    query.prepare(
        QStringLiteral("SELECT size_on_disk, size_fingerprint FROM game_cache WHERE path = ?"));
    query.addBindValue(QString::fromStdString(game_path));

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    if (query.value(0).isNull() || query.value(1).isNull()) {
        return std::nullopt;
    }
    if (query.value(1).toLongLong() != size_fingerprint) {
        return std::nullopt;
    }
    return static_cast<u64>(query.value(0).toLongLong());
}

void GameInfoCache::PutSize(const std::string& game_path, u64 size_on_disk, s64 size_fingerprint) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return;
    }

    QSqlQuery query(conn.Db());
    query.prepare(QStringLiteral(
        "INSERT INTO game_cache (path, fingerprint, size_on_disk, size_fingerprint)"
        " VALUES (?, 0, ?, ?)"
        " ON CONFLICT(path) DO UPDATE SET"
        " size_on_disk=excluded.size_on_disk, size_fingerprint=excluded.size_fingerprint"));
    query.addBindValue(QString::fromStdString(game_path));
    query.addBindValue(static_cast<qint64>(size_on_disk));
    query.addBindValue(static_cast<qint64>(size_fingerprint));

    if (!query.exec()) {
        LOG_ERROR(Frontend, "GameInfoCache: failed to cache size for '{}': {}", game_path,
                  query.lastError().text().toStdString());
    }
}

std::optional<QByteArray> GameInfoCache::GetIcon(const std::string& game_path,
                                                 s64 icon_fingerprint) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return std::nullopt;
    }

    QSqlQuery query(conn.Db());
    query.prepare(
        QStringLiteral("SELECT icon_blob, icon_fingerprint FROM game_cache WHERE path = ?"));
    query.addBindValue(QString::fromStdString(game_path));

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    if (query.value(0).isNull() || query.value(1).isNull()) {
        return std::nullopt;
    }
    if (query.value(1).toLongLong() != icon_fingerprint) {
        return std::nullopt;
    }

    QByteArray data = query.value(0).toByteArray();
    if (data.isEmpty()) {
        return std::nullopt;
    }
    return data;
}

void GameInfoCache::PutIcon(const std::string& game_path, const QByteArray& icon_data,
                            s64 icon_fingerprint) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid() || icon_data.isEmpty()) {
        return;
    }

    QSqlQuery query(conn.Db());
    query.prepare(QStringLiteral(
        "INSERT INTO game_cache (path, fingerprint, icon_blob, icon_fingerprint)"
        " VALUES (?, 0, ?, ?)"
        " ON CONFLICT(path) DO UPDATE SET"
        " icon_blob=excluded.icon_blob, icon_fingerprint=excluded.icon_fingerprint"));
    query.addBindValue(QString::fromStdString(game_path));
    query.addBindValue(icon_data);
    query.addBindValue(static_cast<qint64>(icon_fingerprint));

    if (!query.exec()) {
        LOG_ERROR(Frontend, "GameInfoCache: failed to cache icon for '{}': {}", game_path,
                  query.lastError().text().toStdString());
    }
}

void GameInfoCache::Put(const GameInfo& info, s64 fingerprint) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return;
    }

    QSqlQuery query(conn.Db());
    query.prepare(QStringLiteral(
        "INSERT INTO game_cache (path, fingerprint, serial, name, category, app_ver, sdk_ver,"
        " fw, region, save_dir, icon_path, pic_path, snd0_path, np_comm_ids)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(path) DO UPDATE SET"
        " fingerprint=excluded.fingerprint, serial=excluded.serial, name=excluded.name,"
        " category=excluded.category, app_ver=excluded.app_ver, sdk_ver=excluded.sdk_ver,"
        " fw=excluded.fw, region=excluded.region, save_dir=excluded.save_dir,"
        " icon_path=excluded.icon_path, pic_path=excluded.pic_path,"
        " snd0_path=excluded.snd0_path, np_comm_ids=excluded.np_comm_ids"));
    query.addBindValue(QString::fromStdString(info.path));
    query.addBindValue(static_cast<qint64>(fingerprint));
    query.addBindValue(QString::fromStdString(info.serial));
    query.addBindValue(QString::fromStdString(info.name));
    query.addBindValue(QString::fromStdString(info.category));
    query.addBindValue(QString::fromStdString(info.app_ver));
    query.addBindValue(QString::fromStdString(info.sdk_ver));
    query.addBindValue(QString::fromStdString(info.fw));
    query.addBindValue(QString::fromStdString(info.region));
    query.addBindValue(QString::fromStdString(info.save_dir));
    query.addBindValue(QString::fromStdString(info.icon_path));
    query.addBindValue(QString::fromStdString(info.pic_path));
    query.addBindValue(QString::fromStdString(info.snd0_path));
    query.addBindValue(JoinNpCommIds(info.np_comm_ids));

    if (!query.exec()) {
        LOG_ERROR(Frontend, "GameInfoCache: failed to cache '{}': {}", info.path,
                  query.lastError().text().toStdString());
    }
}

void GameInfoCache::Prune(const std::vector<std::string>& known_paths) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return;
    }

    QSqlDatabase db = conn.Db();
    QSqlQuery query(db);

    db.transaction();
    query.exec(
        QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS keep_paths (path TEXT PRIMARY KEY)"));
    query.exec(QStringLiteral("DELETE FROM keep_paths"));

    if (!known_paths.empty()) {
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO keep_paths (path) VALUES (?)"));
        QVariantList path_list;
        path_list.reserve(static_cast<int>(known_paths.size()));
        for (const auto& path : known_paths) {
            path_list.append(QString::fromStdString(path));
        }
        query.addBindValue(path_list);
        query.execBatch();
    }

    query.exec(
        QStringLiteral("DELETE FROM game_cache WHERE path NOT IN (SELECT path FROM keep_paths)"));
    query.exec(QStringLiteral("DROP TABLE keep_paths"));
    db.commit();
}

void GameInfoCache::Clear() {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return;
    }
    QSqlQuery query(conn.Db());
    if (!query.exec(QStringLiteral("DELETE FROM game_cache"))) {
        LOG_ERROR(Frontend, "GameInfoCache: failed to clear cache: {}",
                  query.lastError().text().toStdString());
    }
}

void GameInfoCache::ClearGame(const std::string& game_path) {
    Connection& conn = ThreadConnection();
    if (!conn.IsValid()) {
        return;
    }
    QSqlQuery query(conn.Db());
    query.prepare(QStringLiteral("DELETE FROM game_cache WHERE path = ?"));
    query.addBindValue(QString::fromStdString(game_path));
    if (!query.exec()) {
        LOG_ERROR(Frontend, "GameInfoCache: failed to clear cache for '{}': {}", game_path,
                  query.lastError().text().toStdString());
    }
}
