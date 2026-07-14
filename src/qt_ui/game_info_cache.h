// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
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

    void WarmUp();
    std::optional<GameInfo> Get(const std::string& game_path, s64 fingerprint);
    void Put(const GameInfo& info, s64 fingerprint);
    std::optional<u64> GetSize(const std::string& game_path, s64 size_fingerprint);
    void PutSize(const std::string& game_path, u64 size_on_disk, s64 size_fingerprint);
    std::optional<QByteArray> GetIcon(const std::string& game_path, s64 icon_fingerprint);
    void PutIcon(const std::string& game_path, const QByteArray& icon_data, s64 icon_fingerprint);
    void Prune(const std::vector<std::string>& known_paths);
    void Clear();
    void ClearGame(const std::string& game_path);

private:
    class Connection;
    Connection& ThreadConnection();
    std::filesystem::path m_db_path;
    QString m_connection_prefix;
};
