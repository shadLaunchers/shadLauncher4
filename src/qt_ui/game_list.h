// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QAction>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QTableWidget>

#include "game_list_base.h"

#include <functional>

class GameItem;

class GameList : public QTableWidget, public GameListBase {
    Q_OBJECT

public:
    GameList();

    void SyncHeaderActions(QList<QAction*>& actions, std::function<bool(int)> get_visibility);
    void CreateHeaderActions(QList<QAction*>& actions, std::function<bool(int)> get_visibility,
                             std::function<void(int, bool)> set_visibility);

    void ClearList() override; // Use this instead of clearContents

    /** Fix columns with width smaller than the minimal section size */
    void FixNarrowColumns();

public Q_SLOTS:
    void FocusAndSelectFirstEntryIfNoneIs();

Q_SIGNALS:
    void FocusToSearchBar();
    void IconReady(const game_info& game, const GameItemBase* item);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
