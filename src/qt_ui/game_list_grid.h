// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "flow_widget.h"
#include "game_list_base.h"
#include "game_list_frame.h"

#include <QKeyEvent>

class GameListGrid : public FlowWidget, public GameListBase {
    Q_OBJECT

public:
    explicit GameListGrid(GameListFrame* frame, std::shared_ptr<GUISettings> gui_settings);

    void ClearList() override;

    void Populate(const std::vector<game_info>& game_data,
                  const std::map<QString, QString>& notes_map,
                  const std::map<QString, QString>& title_map,
                  const std::string& selected_item_id) override;

    void RepaintIcons(std::vector<game_info>& game_data, const QColor& icon_color,
                      const QSize& icon_size, qreal device_pixel_ratio) override;

    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

public Q_SLOTS:
    void FocusAndSelectFirstEntryIfNoneIs();

Q_SIGNALS:
    void FocusToSearchBar();
    void ItemDoubleClicked(const game_info& game);
    void ItemSelectionChanged(const game_info& game);
    void IconReady(const game_info& game, const GameItemBase* item);

private:
    GameListFrame* m_game_list_frame{};
    std::shared_ptr<GUISettings> m_gui_settings;

protected:
    void paintEvent(QPaintEvent* event) override;
};
