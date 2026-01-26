// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <string>
#include "common/types.h"

namespace FS {
namespace Utils {

u64 GetDirSize(const std::string& path, u64 rounding_alignment, std::atomic<bool>* cancel_flag);

}
} // namespace FS