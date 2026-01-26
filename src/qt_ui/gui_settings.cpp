// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/path_util.h"
#include "gui_settings.h"

namespace GUI {

QString stylesheet;
bool custom_stylesheet_active = false;

QString GetGameListColumnName(GameListColumns col) {
    switch (col) {
    case GameListColumns::icon:
        return "column_icon";
    case GameListColumns::name:
        return "column_name";
    case GameListColumns::compat:
        return "column_compat";
    case GameListColumns::serial:
        return "column_serial";
    case GameListColumns::region:
        return "column_region";
    case GameListColumns::firmware:
        return "column_firmware";
    case GameListColumns::version:
        return "column_version";
    case GameListColumns::last_play:
        return "column_last_play";
    case GameListColumns::play_time:
        return "column_play_time";
    case GameListColumns::dir_size:
        return "column_dir_size";
    case GameListColumns::path:
        return "column_path";
    case GameListColumns::count:
        return "";
    }
}

}; // namespace GUI

GUISettings::GUISettings(QObject* parent) : Settings(parent) {
    m_settings = std::make_unique<QSettings>(ComputeSettingsDir() + GUI::settings + ".ini",
                                             QSettings::Format::IniFormat, parent);
}

bool GUISettings::GetGamelistColVisibility(GUI::GameListColumns col) const {
    return GetValue(GetGuiSaveForGameColumn(col)).toBool();
}

void GUISettings::SetGamelistColVisibility(GUI::GameListColumns col, bool val) const {
    SetValue(GetGuiSaveForGameColumn(col), val);
}

QSize GUISettings::GetSizeFromSlider(int pos) {
    return GUI::game_list_icon_size_min +
           (GUI::game_list_icon_size_max - GUI::game_list_icon_size_min) *
               (1.f * pos / GUI::game_list_max_slider_pos);
}

GUISave GUISettings::GetGuiSaveForGameColumn(GUI::GameListColumns col) {
    return GUISave{GUI::game_list, "visibility_" + GUI::GetGameListColumnName(col), true};
}

QString GUISettings::GetVersionExecutablePath(const QString& versionName) const {
    const auto versionsFolder =
        Common::FS::PathFromQString(GetValue(GUI::version_manager_versionPath).toString());
    const auto versionFolder = versionsFolder / Common::FS::PathFromQString(versionName);

    std::string exeName;
#ifdef Q_OS_WIN
    exeName = "shadPS4.exe";
#elif defined(Q_OS_LINUX)
    exeName = "Shadps4-sdl.AppImage";
#elif defined(Q_OS_MACOS)
    exeName = "shadps4";
#endif

    QString result;
    Common::FS::PathToQString(result, versionFolder / exeName);
    return result;
}
