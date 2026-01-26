// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include <QString>

namespace Common::FS {

enum class PathType {
    UserDir,            // Where shadPS4 stores its data.
    VersionDir,         // Where emulator versions are stored.
    AddonDir,           // Where DLCs are installed.
    HomeDir,            // PS4 home directory
    SysModuleDir,       // Where system modules are stored.
    LogDir,             // Where log files are stored.
    CustomConfigs,      // Where custom files for different games are stored.
    CustomInputConfigs, // Where custom input config files for different games are stored.
    PatchesDir,         // Where patches are stored.
    CheatsDir,          // Where cheats are stored.
    CustomTrophy,       // Where custom files for trophies are stored.
    CacheDir,           // Where pipeline and shader cache is stored.
};

// Sub-directories contained within a user data directory
constexpr auto VERSION_DIR = "versions";
constexpr auto ADDON_DIR = "addcont";
constexpr auto HOME_DIR = "home";
constexpr auto SYSMODULES_DIR = "sys_modules";
constexpr auto LOG_DIR = "log";
constexpr auto CUSTOM_CONFIGS = "custom_configs";
constexpr auto CUSTOM_INPUT_CONFIGS = "custom_input_configs";
constexpr auto CUSTOM_TROPHY = "custom_trophy";
constexpr auto PATCHES_DIR = "patches";
constexpr auto CHEATS_DIR = "cheats";
constexpr auto CACHE_DIR = "cache";

// Filenames
constexpr auto LOG_FILE = "shadLauncher4.txt";

/**
 * Gets the filesystem path associated with the PathType enum.
 *
 * @param user_path PathType enum
 *
 * @returns The filesystem path associated with the PathType enum.
 */
[[nodiscard]] const std::filesystem::path& GetUserPath(PathType user_path);

/**
 * Converts a QString to an std::filesystem::path.
 * The native underlying string of a path is wstring on Windows and string on POSIX.
 *
 * @param path The path to convert
 */
[[nodiscard]] std::filesystem::path PathFromQString(const QString& path);

/**
 * Converts an std::filesystem::path to a QString.
 * The native underlying string of a path is wstring on Windows and string on POSIX.
 *
 * @param result The resulting QString
 * @param path The path to convert
 */
void PathToQString(QString& result, const std::filesystem::path& path);

QString QStringFromPath(const std::filesystem::path& path);

std::string PathToUTF8String(const std::filesystem::path& path);

[[nodiscard]] std::optional<std::filesystem::path> FindGameByID(const std::filesystem::path& dir,
                                                                const std::string& game_id,
                                                                int max_depth);

} // namespace Common::FS
