// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "game_item_base.h"

#include <QTableWidgetItem>

class GameItem : public QTableWidgetItem, public GameItemBase {
public:
    GameItem();
    GameItem(const QString& text, int type = Type);
    GameItem(const QIcon& icon, const QString& text, int type = Type);
};
