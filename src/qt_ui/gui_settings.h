// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings.h"

#include <QColor>
#include <QMessageBox>
#include <QSize>
#include <QVariant>
#include <QWindow>

namespace GUI {

extern QString stylesheet;
extern bool custom_stylesheet_active;

enum CustomRoles {
    game_role = Qt::UserRole + 1337,
};
enum class GameListColumns {
    icon,
    name,
    compat,
    serial,
    region,
    firmware,
    version,
    last_play,
    play_time,
    dir_size,
    path,
    count
};

QString GetGameListColumnName(GameListColumns col);

// icon sizes for the game list
const QSize game_list_icon_size_min = QSize(40, 22);
const QSize game_list_icon_size_small = QSize(80, 44);
const QSize game_list_icon_size_medium = QSize(160, 88);
const QSize game_list_icon_size_max = QSize(320, 176);

const int game_list_max_slider_pos = 100;

inline int GetIndex(const QSize& current) {
    const int size_delta = game_list_icon_size_max.width() - game_list_icon_size_min.width();
    const int current_delta = current.width() - game_list_icon_size_min.width();
    return game_list_max_slider_pos * current_delta / size_delta;
}

const QString settings = "shadLauncher4"; // File name for GUI settings
const QString DefaultStylesheet = "default";
const QString NoStylesheet = "none";
const QString NativeStylesheet = "native";

const QString general = "general";
const QString main_window = "main_window";
const QString game_list = "game_list";
const QString meta = "meta";
const QString localization = "localization";
const QString version_manager = "version_manager";
const QString compatibility = "compatibility";
const QString users = "user_manager";

const QColor game_list_icon_color = QColor(240, 240, 240, 255);

// general settings
const GUISave general_show_changelog = GUISave(general, "show_changelog", false);
const GUISave general_check_gui_updates = GUISave(general, "check_gui_updates", false);
const GUISave general_directory_depth_scanning = GUISave(general, "directory_depth_scanning", 1);
const GUISave general_separate_update_folder = GUISave(general, "separate_update_folder", false);

// compatibility settings
const GUISave compatibility_check_on_startup = GUISave(compatibility, "check_on_startup", true);
const GUISave compatibility_etag = GUISave(compatibility, "etag", "");
const GUISave compatibility_last_modified = GUISave(compatibility, "last_modified", "");
const GUISave compatibility_json_url =
    GUISave(compatibility, "json_url",
            "https://github.com/shadps4-compatibility/shadps4-game-compatibility/releases/latest/"
            "download/compatibility_data.json");
const GUISave compatibility_issues_url =
    GUISave(compatibility, "issues_url",
            "https://github.com/shadps4-compatibility/shadps4-game-compatibility/issues/");

// main window settings
const GUISave main_window_gamelist = GUISave(main_window, "gamelistVisible", true);
const GUISave main_window_toolBarVisible = GUISave(main_window, "toolBarVisible", true);
const GUISave main_window_titleBarsVisible = GUISave(main_window, "titleBarsVisible", true);
const GUISave main_window_geometry = GUISave(main_window, "geometry", QByteArray());
const GUISave main_window_windowState = GUISave(main_window, "windowState", QByteArray());
const GUISave main_window_mwState = GUISave(main_window, "mwState", QByteArray());
const GUISave main_window_showLog = GUISave(main_window, "showLog", true);
const GUISave main_window_dockWidgetSizes =
    GUISave(main_window, "dockWidgetSizes", QVariant::fromValue(QList<int>({800, 200})));

// gamelist settings
const GUISave game_list_sortAsc = GUISave(game_list, "sortAsc", true);
const GUISave game_list_sortCol = GUISave(game_list, "sortCol", 1);
const GUISave game_list_state = GUISave(game_list, "state", QByteArray());
const GUISave game_list_iconSize =
    GUISave(game_list, "iconSize", GetIndex(game_list_icon_size_small));
const GUISave game_list_iconSizeGrid =
    GUISave(game_list, "iconSizeGrid", GetIndex(game_list_icon_size_small));
const GUISave game_list_iconColor = GUISave(game_list, "iconColor", game_list_icon_color);
const GUISave game_list_listMode = GUISave(game_list, "listMode", true);
const GUISave game_list_textFactor = GUISave(game_list, "textFactor", qreal{2.0});
const GUISave game_list_marginFactor = GUISave(game_list, "marginFactor", qreal{0.09});
const GUISave game_list_show_hidden = GUISave(game_list, "show_hidden", false);
const GUISave game_list_hidden_list = GUISave(game_list, "hidden_list", QStringList());
const GUISave game_list_draw_compat = GUISave(game_list, "draw_compat", false);
const GUISave game_list_play_bg = GUISave(game_list, "play_bg", true);
const GUISave game_list_bg_volume = GUISave(game_list, "bg_volume", 100);
const GUISave game_list_showBackgroundImage = GUISave(game_list, "showBackgroundImage", true);
const GUISave game_list_backgroundImageOpacity = GUISave(game_list, "backgroundImageOpacity", 50);

// meta settings
const GUISave meta_enableUIColors = GUISave(meta, "enableUIColors", false);
#ifdef __APPLE__
const GUISave meta_currentStylesheet = GUISave(meta, "currentStylesheet", "native (macOS)");
#else
const GUISave meta_currentStylesheet = GUISave(meta, "currentStylesheet", "DefaultStylesheet");
#endif

// localization settings
const GUISave localization_language = GUISave(localization, "language", "en_US");

// version manager
const GUISave version_manager_versionPath = GUISave(version_manager, "versionPath", "");
const GUISave version_manager_versionSelected = GUISave(version_manager, "versionSelected", "");
const GUISave version_manager_showChangeLog = GUISave(version_manager, "showChangeLog", "");
const GUISave version_manager_checkOnStartup = GUISave(version_manager, "checkOnStartup", "");

// user manager
const GUISave user_manager_geometry = GUISave(users, "geometry", QByteArray());

} // namespace GUI

/** Class for GUI settings..
 */
class GUISettings : public Settings {
    Q_OBJECT

public:
    explicit GUISettings(QObject* parent = nullptr);
    static QSize GetSizeFromSlider(int pos);
    bool GetGamelistColVisibility(GUI::GameListColumns col) const;
    void SetGamelistColVisibility(GUI::GameListColumns col, bool val) const;

    QString GetVersionExecutablePath(const QString& versionName) const;

private:
    static GUISave GetGuiSaveForGameColumn(GUI::GameListColumns col);
};
