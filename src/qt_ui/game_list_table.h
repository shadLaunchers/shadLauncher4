// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "game_list.h"

class GameListFrame;
class QTimer;

class GameListTable : public GameList {
    Q_OBJECT

public:
    GameListTable(GameListFrame* frame, std::shared_ptr<GUISettings> gui_settings);

    /** Restores the initial layout of the table */
    void restoreLayout(const QByteArray& state);

    /** Resizes the columns to their contents and adds a small spacing */
    void resizeColumnsToContents(int spacing = 20);

    void adjustIconColumn();

    void sort(u64 game_count, int sort_column, Qt::SortOrder col_sort_order);

    void SetCustomConfigIcon(const game_info& game);

    void Populate(const std::vector<game_info>& game_data,
                  const std::map<QString, QString>& notes_map,
                  const std::map<QString, QString>& title_map,
                  const std::string& selected_item_id) override;

    void RepaintIcons(std::vector<game_info>& game_data, const QColor& icon_color,
                      const QSize& icon_size, qreal device_pixel_ratio) override;

    // Updates the compatibility cell (text/tooltip/color dot) of every
    // existing row in place, reading the already-refreshed game->compat
    // field off the game_info stored on the icon cell.
    void UpdateCompatItems();

Q_SIGNALS:
    void sizeOnDiskReady(const game_info& game, GameItemBase* item,
                         std::shared_ptr<std::atomic<bool>> cancel);

private:
    GameListFrame* m_game_list_frame{};
    std::shared_ptr<GUISettings> m_gui_settings;
    QTimer* m_resort_timer = nullptr;

protected:
    void paintEvent(QPaintEvent* event) override;
};
