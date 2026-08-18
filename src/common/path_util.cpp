// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <unordered_map>
#include "common/path_util.h"
#include "core/file_sys/game_backend.h"

namespace Common::FS {

namespace fs = std::filesystem;

static auto UserPaths = [] {
    auto user_dir = std::filesystem::current_path() / "user";

    std::unordered_map<PathType, fs::path> paths;

    const auto create_path = [&](PathType shad_path, const fs::path& new_path) {
        std::filesystem::create_directory(new_path);
        paths.insert_or_assign(shad_path, new_path);
    };

    create_path(PathType::UserDir, user_dir);
    create_path(PathType::VersionDir, user_dir / VERSION_DIR);
    create_path(PathType::AddonDir, user_dir / ADDON_DIR);
    create_path(PathType::HomeDir, user_dir / HOME_DIR);
    create_path(PathType::SysModuleDir, user_dir / SYSMODULES_DIR);
    create_path(PathType::LogDir, user_dir / LOG_DIR);
    create_path(PathType::CustomConfigs, user_dir / CUSTOM_CONFIGS);
    create_path(PathType::CustomInputConfigs, user_dir / CUSTOM_INPUT_CONFIGS);
    create_path(PathType::CustomTrophy, user_dir / CUSTOM_TROPHY);
    create_path(PathType::PatchesDir, user_dir / PATCHES_DIR);
    create_path(PathType::CheatsDir, user_dir / CHEATS_DIR);
    create_path(PathType::CacheDir, user_dir / CACHE_DIR);
    create_path(PathType::FontsDir, user_dir / FONTS_DIR);
    create_path(PathType::ThemesDir, user_dir / THEMES_DIR);

    return paths;
}();

const fs::path& GetUserPath(PathType shad_path) {
    return UserPaths.at(shad_path);
}

std::filesystem::path PathFromQString(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

void PathToQString(QString& result, const std::filesystem::path& path) {
#ifdef _WIN32
    result = QString::fromStdWString(path.wstring());
#else
    result = QString::fromStdString(path.string());
#endif
}

QString QStringFromPath(const std::filesystem::path& path) {
    return QString::fromStdString(path.string());
}

std::string PathToUTF8String(const std::filesystem::path& path) {
    const auto u8_string = path.u8string();
    return std::string{u8_string.begin(), u8_string.end()};
}

std::optional<fs::path> FindGameByID(const fs::path& dir, const std::string& game_id,
                                     int max_depth) {
    if (max_depth < 0) {
        return std::nullopt;
    }

    const auto is_game_root = [](const fs::path& root) {
        return Core::FileSys::ReadGameFile(root, "sce_sys/param.sfo").has_value();
    };

    // Check if this is the game we're looking for
    if (dir.filename() == game_id && is_game_root(dir)) {
        return dir;
    }
    {
        std::error_code dir_ec;
        if (const auto folder_candidate = dir / game_id;
            fs::is_directory(folder_candidate, dir_ec) && !dir_ec &&
            is_game_root(folder_candidate)) {
            return folder_candidate;
        }
        if (const auto zar_candidate = dir / (game_id + ".zar");
            Core::FileSys::IsZArchiveFile(zar_candidate) && is_game_root(zar_candidate)) {
            return zar_candidate;
        }
    }

    // Recursively search subdirectories
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code entry_ec;
        if (!entry.is_directory(entry_ec) || entry_ec) {
            continue;
        }
        if (auto found = FindGameByID(entry.path(), game_id, max_depth - 1)) {
            return found;
        }
    }

    return std::nullopt;
}

} // namespace Common::FS
