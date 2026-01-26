// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "flow_widget_item.h"
#include "game_item_base.h"
#include "game_list_base.h"

#include <QLabel>

class GameListGridItem : public FlowWidgetItem, public GameItemBase {
    Q_OBJECT

public:
    GameListGridItem(QWidget* parent, game_info game, const QString& title);

    void SetIconSize(const QSize& size);
    void SetIcon(const QPixmap& pixmap);
    void AdjustSize();

    const game_info& Game() const {
        return m_game;
    }

    void ShowTitle(bool visible);

    void PolishStyle() override;

    bool event(QEvent* event) override;

private:
    QSize m_icon_size{};
    QLabel* m_icon_label{};
    QLabel* m_title_label{};
    game_info m_game{};
};
