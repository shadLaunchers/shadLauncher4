// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMenu>
#include <QPoint>

#include "gui_game_info.h"

class GameListFrame;

class GameListContextMenu : public QMenu {
    Q_OBJECT
public:
    explicit GameListContextMenu(GameListFrame* frame);

    // Builds the menu for the given game and shows it (blocking) at
    // global_pos.
    void Show(const game_info& gameinfo, const QPoint& global_pos);

private:
    GameListFrame* m_frame = nullptr;
};
