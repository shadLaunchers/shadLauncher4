// SPDX-FileCopyrightText: Copyright 2025 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "game_install_dialog.h"
#include "gui_settings.h"
#include "qt_utils.h"
#include "ui_game_install_dialog.h"

GameInstallDialog::GameInstallDialog(std::shared_ptr<GUISettings> gui_settings,
                                     std::shared_ptr<EmulatorSettings> emu_settings,
                                     QWidget* parent)
    : QDialog(parent), ui(new Ui::GameInstallDialog), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emu_settings)) {
    ui->setupUi(this);

    // --- Games directory ---
    if (!m_emu_settings->GetGameInstallDirs().empty()) {
        QString tmp;
        Common::FS::PathToQString(
            tmp, GUI::Utils::NormalizePath(m_emu_settings->GetGameInstallDirs().front()));
        ui->editGamesDirectory->setText(tmp);
    } else {
        // Default: empty or some fallback
        ui->editGamesDirectory->setText(QString());
    }

    // --- Addons directory ---
    if (!m_emu_settings->GetAddonInstallDir().empty()) {
        QString tmp;
        Common::FS::PathToQString(tmp,
                                  GUI::Utils::NormalizePath(m_emu_settings->GetAddonInstallDir()));
        ui->editAddonsDirectory->setText(tmp);
    } else {
        // Default addon path
        std::filesystem::path defaultAddonPath =
            GUI::Utils::NormalizePath(Common::FS::GetUserPath(Common::FS::PathType::AddonDir));
#ifdef _WIN32
        ui->editAddonsDirectory->setText(QString::fromStdWString(defaultAddonPath.wstring()));
#else
        ui->editAddonsDirectory->setText(QString::fromUtf8(defaultAddonPath.u8string().c_str()));
#endif
    }

    // --- Version directory ---
    QString version = m_gui_settings->GetValue(GUI::vm_versionPath).toString();
    if (!version.isEmpty()) {
        ui->editVersionDirectory->setText(version);
    } else {
        std::filesystem::path defaultVersionPath =
            GUI::Utils::NormalizePath(Common::FS::GetUserPath(Common::FS::PathType::VersionDir));
#ifdef _WIN32
        ui->editVersionDirectory->setText(QString::fromStdWString(defaultVersionPath.wstring()));
#else
        ui->editVersionDirectory->setText(QString::fromUtf8(defaultVersionPath.u8string().c_str()));
#endif
    }

    // Connect browse buttons
    connect(ui->btnBrowseGames, &QPushButton::clicked, this,
            &GameInstallDialog::BrowseGamesDirectory);
    connect(ui->btnBrowseAddons, &QPushButton::clicked, this,
            &GameInstallDialog::BrowseAddonsDirectory);
    connect(ui->btnBrowseVersions, &QPushButton::clicked, this,
            &GameInstallDialog::BrowseVersionDirectory);

    // Connect OK/Cancel buttons
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &GameInstallDialog::Save);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &GameInstallDialog::reject);
}

GameInstallDialog::~GameInstallDialog() {
    delete ui;
}

void GameInstallDialog::BrowseGamesDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Directory with your dumped games"));
    if (!dir.isEmpty())
        ui->editGamesDirectory->setText(dir);
}

void GameInstallDialog::BrowseAddonsDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Directory with your dumped DLCs"));
    if (!dir.isEmpty())
        ui->editAddonsDirectory->setText(dir);
}

void GameInstallDialog::BrowseVersionDirectory() {
    QString dir =
        QFileDialog::getExistingDirectory(this, tr("Directory to install emulator versions"));
    if (!dir.isEmpty())
        ui->editVersionDirectory->setText(dir);
}

void GameInstallDialog::Save() {
    QString g = ui->editGamesDirectory->text().trimmed();
    QString a = ui->editAddonsDirectory->text().trimmed();
    QString v = ui->editVersionDirectory->text().trimmed();

    auto gamesPath = GUI::Utils::NormalizePath(Common::FS::PathFromQString(g));
    auto addonsPath = GUI::Utils::NormalizePath(Common::FS::PathFromQString(a));
    auto versionPath = GUI::Utils::NormalizePath(Common::FS::PathFromQString(v));

    // Validate directories
    if (g.isEmpty() || !QDir(g).exists() || !QDir::isAbsolutePath(g)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The chosen location for dumped games is not valid."));
        return;
    }

    if (a.isEmpty() || !QDir::isAbsolutePath(a)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The chosen location for dumped DLCs is not valid."));
        return;
    }

    QDir addonsDir(a);
    if (!addonsDir.exists() && !addonsDir.mkpath(".")) {
        QMessageBox::critical(this, tr("Error"), tr("The DLC dump location could not be created."));
        return;
    }

    if (v.isEmpty() || !QDir::isAbsolutePath(v)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The location for installing emulator versions is not valid."));
        return;
    }

    QDir versionDir(v);
    if (!versionDir.exists() && !versionDir.mkpath(".")) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The emulator version location could not be created."));
        return;
    }

    // Save normalized paths
    m_emu_settings->AddGameInstallDir(std::filesystem::path(gamesPath));
    m_emu_settings->SetAddonInstallDir(std::filesystem::path(addonsPath));
    m_gui_settings->SetValue(GUI::vm_versionPath, QString::fromStdString(versionPath));

    m_emu_settings->Save();

    accept();
}
