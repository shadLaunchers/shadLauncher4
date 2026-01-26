// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "game_compatibility.h"
#include "game_info.h"
#include "game_item_base.h"

struct GUIGameInfo {
    GameInfo info{};
    Compat::Status compat;
    QPixmap icon;
    QPixmap pxmap;
    bool has_custom_config = false;
    bool has_custom_pad_config = false;
    GameItemBase* item = nullptr;
};

typedef std::shared_ptr<GUIGameInfo> game_info;
Q_DECLARE_METATYPE(game_info)
