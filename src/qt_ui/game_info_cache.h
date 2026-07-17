// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <QByteArray>
#include <QString>

#include "common/types.h"
#include "game_info.h"

class GameInfoCache {
public:
    explicit GameInfoCache(std::filesystem::path db_path);
    ~GameInfoCache();

    GameInfoCache(const GameInfoCache&) = delete;
    GameInfoCache& operator=(const GameInfoCache&) = delete;

    struct CachedEntry {
        s64 fingerprint;
        GameInfo info;
    };

    void WarmUp();
    std::optional<GameInfo> Get(const std::string& game_path, s64 fingerprint);
    void Put(const GameInfo& info, s64 fingerprint);
    std::unordered_map<std::string, CachedEntry> GetAllMeta();
    void PutMany(const std::vector<std::pair<GameInfo, s64>>& entries);
    std::optional<u64> GetSize(const std::string& game_path, s64 size_fingerprint);
    void PutSize(const std::string& game_path, u64 size_on_disk, s64 size_fingerprint);
    std::optional<QByteArray> GetIcon(const std::string& game_path, s64 icon_fingerprint);
    void PutIcon(const std::string& game_path, const QByteArray& icon_data, s64 icon_fingerprint);
    void Prune(const std::vector<std::string>& known_paths);
    void Clear();
    void ClearGame(const std::string& game_path);

    // Returns one GameInfo per distinct cached serial (deduped, and skipping DLC and
    // -UPDATE/-patch sub-folders so update/patch directories - which share their base game's
    // serial - don't show up as separate rows). Meant only for showing a last-known list
    // instantly at startup before the real scan completes; every field this returns comes
    // straight from the cache, so size/icon may be stale and compat/custom-config decoration
    // isn't included at all - the real scan replaces this within moments.
    std::vector<GameInfo> GetAllForInstantList();

private:
    class Connection;
    Connection& ThreadConnection();
    std::filesystem::path m_db_path;
    QString m_connection_prefix;
};
