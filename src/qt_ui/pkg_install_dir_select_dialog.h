// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QPushButton>
#include <QTableView>

#include <filesystem>
#include <memory>
#include <vector>

class EmulatorSettings;
class QDialogButtonBox;
class PkgInstallModel;
struct PkgInfo;

class PkgInstallDirSelectDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PkgInstallDirSelectDialog(std::shared_ptr<EmulatorSettings> emu_settings,
                                       QWidget* parent = nullptr);
    ~PkgInstallDirSelectDialog() override;

    void SetPkgList(const std::vector<PkgInfo>& pkgs);
    std::vector<PkgInfo> GetSelectedPkgs() const;

    bool GetDeleteFileOnInstall() const {
        return m_delete_file_on_install;
    }
    std::filesystem::path GetSelectedDirectory() const {
        return m_selected_dir;
    }

private:
    // UI setup
    QWidget* SetupGameSelectionTable();
    QWidget* SetupInstallDirSelection(QPushButton* okButton);
    QDialogButtonBox* SetupDialogActions();

    // State updates
    void UpdateOkButtonState(QPushButton* okButton);
    void SetSelectedDirectory(const QString& dir);
    void SetDeleteFileOnInstall(bool enabled);

private:
    // --- Models / Views ---
    QTableView* m_game_view{nullptr};
    PkgInstallModel* m_model{nullptr};

    // --- Data ---
    std::vector<PkgInfo> m_pkgs;
    std::filesystem::path m_selected_dir;
    std::shared_ptr<EmulatorSettings> m_emu_settings;
    bool m_delete_file_on_install{false};
};
