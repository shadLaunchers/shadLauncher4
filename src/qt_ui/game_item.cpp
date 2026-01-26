// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "game_item.h"

GameItem::GameItem() : QTableWidgetItem(), GameItemBase() {}

GameItem::GameItem(const QString& text, int type) : QTableWidgetItem(text, type), GameItemBase() {}

GameItem::GameItem(const QIcon& icon, const QString& text, int type)
    : QTableWidgetItem(icon, text, type), GameItemBase() {}
