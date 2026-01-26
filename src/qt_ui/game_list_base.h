// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gui_game_info.h"

#include <QIcon>
#include <QWidget>

class GameListBase {
public:
    GameListBase();

    virtual void ClearList() {};
    virtual void Populate([[maybe_unused]] const std::vector<game_info>& game_data,
                          [[maybe_unused]] const std::map<QString, QString>& notes_map,
                          [[maybe_unused]] const std::map<QString, QString>& title_map,
                          [[maybe_unused]] const std::string& selected_item_id) {};

    void SetIconSize(QSize size) {
        m_icon_size = std::move(size);
    }
    void SetIconColor(QColor color) {
        m_icon_color = std::move(color);
    }
    void SetDrawCompatStatusToGrid(bool enabled) {
        m_draw_compat_status_to_grid = enabled;
    }

    virtual void RepaintIcons(std::vector<game_info>& game_data, const QColor& icon_color,
                              const QSize& icon_size, qreal device_pixel_ratio);

    /** Sets the custom config icon. */
    static QIcon GetCustomConfigIcon(const game_info& game);

protected:
    void IconLoadFunction(game_info game, qreal device_pixel_ratio,
                          std::shared_ptr<std::atomic<bool>> cancel);
    QPixmap PaintedPixmap(const QPixmap& icon, qreal device_pixel_ratio,
                          bool paint_config_icon = false, bool paint_pad_config_icon = false,
                          const QColor& compatibility_color = {}) const;
    QColor GetGridCompatibilityColor(const QString& string) const;

    std::function<void(const game_info&, const GameItemBase*)> m_icon_ready_callback{};
    bool m_draw_compat_status_to_grid{};
    bool m_is_list_layout{};
    QSize m_icon_size{};
    QColor m_icon_color{};
};
