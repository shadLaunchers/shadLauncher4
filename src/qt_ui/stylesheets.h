// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

namespace GUI {
namespace Stylesheets {
const QString default_style_sheet(

    // Use the desktop palette so Default works with both light and dark themes.
    // main window toolbar search
    "QLineEdit#mw_searchbar { padding: 0 1em; background: palette(base); "
    "selection-background-color: palette(highlight); selection-color: palette(highlighted-text); "
    "margin: .8em; color: palette(text); }"

    // main window toolbar slider
    "QSlider#sizeSlider { color: palette(window-text); background: palette(window); }"
    "QSlider#sizeSlider::handle:horizontal { border: 0em solid palette(mid); "
    "border-radius: .58em; background: palette(window-text); width: 1.2em; margin: -.5em 0; }"
    "QSlider#sizeSlider::groove:horizontal { border-radius: .15em; background: palette(mid); height: "
    ".3em; }"

    // main window toolbar
    "QToolBar#mw_toolbar { background-color: palette(window); border: none; }"
    "QToolBar#mw_toolbar::separator { background-color: palette(mid); width: 0.125em; "
    "margin-top: 0.250em; margin-bottom: 0.250em; }"
    "QToolButton:disabled { color: palette(mid); }"

    // main window toolbar icon color
    "QLabel#toolbar_icon_color { color: palette(window-text); }"

    // thumbnail icon color
    "QLabel#thumbnail_icon_color { color: palette(highlight); }"

    // game list icon color
    "QLabel#gamelist_icon_background_color { color: transparent; }"

    // game grid
    "#GameListGrid { background-color: transparent; }"
    "#flow_widget_content { background-color: transparent; }"
    "#GameListGridItem[selected=\"true\"] { background: palette(highlight); }"
    "#GameListGridItem:focus { border: 2px solid palette(highlight); "
    "background-color: palette(highlight); }"
    "#GameListGridItem:hover { background: palette(highlight); }"
    "#GameListGridItem:hover:focus { background: palette(highlight); }"
    "#GameListGridItem #game_list_grid_item_title_label { color: palette(window-text); "
    "font-weight: 600; font-size: 8pt; font-family: Lucida Grande; border: 0em; }"

    // game grid hover and focus: we need to handle properties differently when using descendants
    "#GameListGridItem[selected=\"true\"] #game_list_grid_item_title_label { "
    "color: palette(highlighted-text); }"
    "#GameListGridItem[hover=\"true\"] #game_list_grid_item_title_label { "
    "color: palette(highlighted-text); }"
    "#GameListGridItem[focus=\"true\"] #game_list_grid_item_title_label { "
    "color: palette(highlighted-text); }"

    // tables
    "QTableWidget { background-color: palette(base); color: palette(text); border: none; }"
    "QTableWidget::item:selected { background-color: palette(highlight); "
    "color: palette(highlighted-text); }"

    // table headers
    "QHeaderView::section { padding-left: .5em; padding-right: .5em; padding-top: .4em; "
    "padding-bottom: -.1em; background: palette(button); color: palette(button-text); "
    "border: 0.063em solid palette(mid); }"
    "QHeaderView::section:hover { background: palette(midlight); "
    "padding-left: .5em; padding-right: .5em; padding-top: .4em; padding-bottom: -.1em; "
    "border: 0.063em solid palette(mid); }"

    // dock widget
    "QDockWidget{ background: transparent; color: palette(window-text); }"
    "QDockWidget[floating=\"true\"]{ background: palette(window); }"
    "QDockWidget::title{ background: palette(button); border: none; "
    "padding-top: 0.2em; padding-left: "
    "0.2em; }"
    "QDockWidget::close-button, QDockWidget::float-button{ background-color: palette(button); }"

    // Top menu bar (Workaround for transparent menus in Qt 6.7.3)
    "QMenuBar { color: palette(window-text); background-color: palette(window); }"
    "QMenuBar::item { background: transparent; }"
    "QMenuBar::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
    "QMenu { color: palette(window-text); background-color: palette(window); "
    "alternate-background-color: palette(alternate-base); }"
    "QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
    "QMenu::item:disabled { color: palette(mid); }");
}
} // namespace GUI
