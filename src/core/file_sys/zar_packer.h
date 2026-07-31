// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "common/types.h"

namespace Core::FileSys {

struct PackProgress {
    u64 bytes_done = 0;
    u64 bytes_total = 0;
    std::string current_file;
};

bool PackDirectoryToZArchive(const std::filesystem::path& input_dir,
                             const std::filesystem::path& output_zar,
                             const std::function<bool(const PackProgress&)>& progress_cb,
                             std::string* error_message);

} // namespace Core::FileSys
