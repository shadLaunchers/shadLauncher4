// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

namespace GUI {
namespace Stylesheets {
const QString default_style_sheet(

    // main window toolbar search
    "QLineEdit#mw_searchbar { padding: 0 1em; background: #fdfdfd; selection-background-color: "
    "#148aff; margin: .8em; color:#000000; }"

    // main window toolbar slider
    "QSlider#sizeSlider { color: #505050; background: #F0F0F0; }"
    "QSlider#sizeSlider::handle:horizontal { border: 0em smooth rgba(227, 227, 227, 255); "
    "border-radius: .58em; background: #404040; width: 1.2em; margin: -.5em 0; }"
    "QSlider#sizeSlider::groove:horizontal { border-radius: .15em; background: #5b5b5b; height: "
    ".3em; }"

    // main window toolbar
    "QToolBar#mw_toolbar { background-color: #F0F0F0; border: none; }"
    "QToolBar#mw_toolbar::separator { background-color: rgba(207, 207, 207, 235); width: 0.125em; "
    "margin-top: 0.250em; margin-bottom: 0.250em; }"
    "QToolButton:disabled { color: #787878; }"

    // main window toolbar icon color
    "QLabel#toolbar_icon_color { color: #5b5b5b; }"

    // thumbnail icon color
    "QLabel#thumbnail_icon_color { color: rgba(0, 100, 231, 255); }"

    // game list icon color
    "QLabel#gamelist_icon_background_color { color: rgba(240, 240, 240, 255); }"

    // game grid
    "#GameListGrid { background-color: transparent; }"
    "#flow_widget_content { background-color: transparent; }"
    "#GameListGridItem[selected=\"true\"] { background: lightblue; }"
    "#GameListGridItem:focus { border: 2px solid blue; background-color: lightblue; }"
    "#GameListGridItem:hover { background: #94c9ff; }"
    "#GameListGridItem:hover:focus { background: #007fff; }"
    "#GameListGridItem #game_list_grid_item_title_label { color: rgba(51, 51, 51, 255); "
    "font-weight: 600; font-size: 8pt; font-family: Lucida Grande; border: 0em solid white; }"

    // game grid hover and focus: we need to handle properties differently when using descendants
    "#GameListGridItem[selected=\"true\"] #game_list_grid_item_title_label { color: #fff; }"
    "#GameListGridItem[hover=\"true\"] #game_list_grid_item_title_label { color: #fff; }"
    "#GameListGridItem[focus=\"true\"] #game_list_grid_item_title_label { color: #fff; }"

    // tables
    "QTableWidget { background-color: #fff; border: none; }"
    "QTableWidget::item:selected { background-color: #148aff; color: #fff; }"

    // table headers
    "QHeaderView::section { padding-left: .5em; padding-right: .5em; padding-top: .4em; "
    "padding-bottom: -.1em; border: 0.063em solid #ffffff; }"
    "QHeaderView::section:hover { background: #e3e3e3; padding-left: .5em; padding-right: .5em; "
    "padding-top: .4em; padding-bottom: -.1em; border: 0.063em solid #ffffff; }"

    // dock widget
    "QDockWidget{ background: transparent; color: black; }"
    "[floating=\"true\"]{ background: white; }"
    "QDockWidget::title{ background: #e3e3e3; border: none; padding-top: 0.2em; padding-left: "
    "0.2em; }"
    "QDockWidget::close-button, QDockWidget::float-button{ background-color: #e3e3e3; }"

    // Top menu bar (Workaround for transparent menus in Qt 6.7.3)
    "QMenu { color: #000; background-color: #F0F0F0; alternate-background-color: #f2f2f2; }"
    "QMenu::item:selected { background: #90C8F6; }"
    "QMenu::item:disabled { color: #787878; }");
}
} // namespace GUI
