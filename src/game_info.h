// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <string>
#include <vector>
#include "common/types.h"

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
