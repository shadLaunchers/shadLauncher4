// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <QString>

#include "common/types.h"
#include "core/file_format/psf.h"
#include "core/file_sys/game_backend.h"

struct GameInfo {
    std::string path; // root path of game directory (normaly directory that contains eboot.bin)
    std::string icon_path;   // path of icon0.png
    std::string update_path; // path of update directory if any
    std::string pic_path;    // path of pic0.png
    std::string snd0_path;   // path of snd0.at9

    std::string name;
    std::string serial;
    std::string app_ver;
    std::string region;
    std::string fw;
    std::string save_dir;
    std::string category;
    std::string sdk_ver;
    std::vector<std::string>
        np_comm_ids; // normally there is only one np_comm_id, but found games with multiple ids

    u64 size_on_disk = UINT64_MAX;
};

namespace GameInfoTools {
static QString GetRegion(char region) {
    switch (region) {
    case 'U':
        return "USA";
    case 'E':
        return "Europe";
    case 'J':
        return "Japan";
    case 'H':
        return "Asia";
    case 'I':
        return "World";
    default:
        return "Unknown";
    }
}

// Strips a trailing ".zar" extension so overlay suffixes
static std::filesystem::path StripZArchiveExtension(const std::filesystem::path& path) {
    if (path.extension() == ".zar") {
        std::filesystem::path stripped = path;
        stripped.replace_extension();
        return stripped;
    }
    return path;
}

static void SceUpdateChecker(const std::string sceItem, std::string& gameItem,
                             std::filesystem::path& update_folder,
                             std::filesystem::path& patch_folder, std::string& game_folder) {

    std::filesystem::path game_folder_path = game_folder;
    const std::string rel_path = "sce_sys/" + sceItem;

    for (const auto& candidate : {update_folder, patch_folder, game_folder_path}) {
        if (const auto root = Core::FileSys::ResolveGameRoot(candidate)) {
            if (const auto resolved = Core::FileSys::ResolveGameFilePath(*root, rel_path)) {
                gameItem = resolved->string();
                return;
            }
        }
    }
    gameItem = (game_folder_path / "sce_sys" / sceItem).string();
}

static GameInfo readGameInfo(const std::filesystem::path& filePath) {
    GameInfo game;
    game.path = filePath.string();
    std::string param_sfo_path;
    const std::filesystem::path stem_path = StripZArchiveExtension(filePath);
    std::filesystem::path game_update_path = stem_path;
    game_update_path += "-UPDATE";
    std::filesystem::path game_patch_path = stem_path;
    game_patch_path += "-patch";
    SceUpdateChecker("param.sfo", param_sfo_path, game_update_path, game_patch_path, game.path);

    PSF psf;
    if (psf.Open(param_sfo_path)) {
        SceUpdateChecker("icon0.png", game.icon_path, game_update_path, game_patch_path, game.path);
        SceUpdateChecker("pic1.png", game.pic_path, game_update_path, game_patch_path, game.path);
        SceUpdateChecker("snd0.at9", game.snd0_path, game_update_path, game_patch_path, game.path);

        if (const auto title = psf.GetString("TITLE"); title.has_value()) {
            game.name = *title;
        }
        if (const auto title_id = psf.GetString("TITLE_ID"); title_id.has_value()) {
            game.serial = *title_id;
        }
        if (const auto content_id = psf.GetString("CONTENT_ID");
            content_id.has_value() && !content_id->empty()) {
            game.region = GetRegion(content_id->at(0)).toStdString();
        }
        if (const auto fw_int_opt = psf.GetInteger("SYSTEM_VER"); fw_int_opt.has_value()) {
            uint32_t fw_int = *fw_int_opt;
            if (fw_int == 0) {
                game.fw = "0.00";
            } else {
                u8 major_bcd = (fw_int >> 24) & 0xFF;
                u8 minor_bcd = (fw_int >> 16) & 0xFF;

                int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
                int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);

                QString fw = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
                game.fw = fw.toStdString();
            }
        }

        if (auto app_ver = psf.GetString("APP_VER"); app_ver.has_value()) {
            game.app_ver = *app_ver;
        }
        if (const auto save_dir = psf.GetString("INSTALL_DIR_SAVEDATA"); save_dir.has_value()) {
            game.save_dir = *save_dir;
        } else {
            game.save_dir = game.serial;
        }
    }
    return game;
}

} // namespace GameInfoTools
