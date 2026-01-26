// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QActionGroup>
#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QMimeData>
#include <QUrl>

#include "core/ipc/ipc_client.h"
#include "gui_game_info.h"

class EmulatorSettings;
class GUISettings;
class PersistentSettings;
class GameListFrame;
class IpcClient;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
    std::unique_ptr<Ui::MainWindow> ui;
    bool m_save_slider_pos = false;
    bool m_is_list_mode = true;
    int m_other_slider_pos = 0;

public:
    MainWindow(std::shared_ptr<GUISettings> gui_settings,
               std::shared_ptr<EmulatorSettings> emu_settings,
               std::shared_ptr<PersistentSettings> persistent_settings,
               std::shared_ptr<IpcClient> ipc_client, QWidget* parent = nullptr);
    ~MainWindow();
    bool init();
    void InstallDragDropPkgs(const std::vector<std::filesystem::path>& files);
    void InstallSinglePkg(std::filesystem::path file, int pkgNum, int nPkg);
Q_SIGNALS:
    void requestLanguageChange(const QString& language);
    void RequestGlobalStylesheetChange();
    void ExtractionFinished();

public Q_SLOTS:
    void retranslateUI(const QStringList& language_codes, const QString& language_code);
    void resizeIcons(int index);
    void setIconSizeActions(int idx) const;
    void RepaintGUI();
    void StartGameWithArgs(const game_info& game, QStringList args = {});
    void StartEmulator(std::filesystem::path path, QStringList args = {});
    void RestartGame();
    void PauseGame();
    void StopGame();
    void ToggleFullscreen();

private Q_SLOTS:
    void saveWindowState() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    // these used in pkg drag and drop
    std::filesystem::path last_install_dir = "";
    bool delete_file_on_install = false;
    bool use_for_all_queued = false;

private:
    void showTitleBars(bool show) const;
    void configureGuiFromSettings();
    void createDockWindows();
    void createActions();
    void createConnects();
    void LoadVersionComboBox();
    void updateLanguageActions(const QStringList& language_codes, const QString& language_code);
    void InstallPkg();
    void RunGame();
    void onGameClosed();
    void RestartEmulator();

    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettings> m_emu_settings;
    std::shared_ptr<PersistentSettings> m_persistent_settings;
    QMainWindow* m_mw = nullptr;
    GameListFrame* m_game_list_frame = nullptr;
    QActionGroup* m_icon_size_act_group = nullptr;
    QActionGroup* m_list_mode_act_group = nullptr;

    // IPC things
    std::shared_ptr<IpcClient> m_ipc_client = std::make_shared<IpcClient>();
    game_info last_game_info;
    bool is_paused;
};
