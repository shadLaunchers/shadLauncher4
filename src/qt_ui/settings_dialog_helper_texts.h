// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class SettingsDialogHelperTexts : public QObject {
    Q_OBJECT

public:
    SettingsDialogHelperTexts();

    const struct settings {
        // clang-format off
        //general
        const QString general_show_splash = tr("Show Splash Screen:\\nShows the game's splash screen (a special image) while the game is starting.");
        const QString general_disable_trophy_popup= tr("Disable Trophy Pop-ups:\\nDisable in-game trophy notifications. Trophy progress can still be tracked using the Trophy Viewer (right-click the game in the main window).");
        const QString general_volume_slider = tr("Volume:\\nAdjust volume for games on a global level, range goes from 0-500% with the default being 100%.");       
        //paths
        const QString paths_gameDir = tr("Game Folders:\\nThe list of folders to check for installed games.");
        const QString paths_gameDir_add = tr("Add Folder:\\nAdd a new folder to the list of game installation folders.");
        const QString paths_gameDir_remove = tr("Remove Folder:\\nRemove the selected folder from the list of game installation folders.");
        const QString paths_dlcDir = tr("DLC Path:\\nThe folder where game DLC is loaded from.");
        const QString paths_dlcDir_browse = tr("Browse:\\nBrowse for a folder to set as the DLC path.");
        const QString paths_homeDir = tr("Home Folder:\\nThe folder where the emulator stores user data such as save files and trophies.");
        const QString paths_homeDir_browse = tr("Browse:\\nBrowse for a folder to set as the Home folder.");
        const QString paths_sysmodulesDir = tr("System Modules Folder:\\nThe folder where system modules are loaded from.");
        const QString paths_sysmodulesDir_browse = tr("Browse:\\nBrowse for a folder to set as the System Modules folder.");
        const QString paths_fontsDir = tr("System Fonts Folder:\\nThe folder where system fonts are loaded from.");
        const QString paths_fontsDir_browse = tr("Browse:\\nBrowse for a folder to set as the System Fonts folder.");
        //gui
        const QString general_scan_depth_combo = tr("Directory Scan Depth:\\nSet the maximum depth when scanning for games in the specified game folders.\\n1 means one level of subfolders is scanned, and so on.");
        // clang-format on
    } settings;
};