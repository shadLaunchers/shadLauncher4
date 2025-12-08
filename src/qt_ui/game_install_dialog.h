// SPDX-FileCopyrightText: Copyright 2025 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <QDialog>

namespace Ui {
class GameInstallDialog;
}

class GUISettings;
class EmulatorSettings;

class GameInstallDialog : public QDialog {
    Q_OBJECT

public:
    explicit GameInstallDialog(std::shared_ptr<GUISettings> gui_settings,
                               std::shared_ptr<EmulatorSettings> emu_settings,
                               QWidget* parent = nullptr);
    ~GameInstallDialog() override;

private slots:
    void BrowseGamesDirectory();
    void BrowseAddonsDirectory();
    void BrowseVersionDirectory();
    void Save();

private:
    Ui::GameInstallDialog* ui;

    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettings> m_emu_settings;
};