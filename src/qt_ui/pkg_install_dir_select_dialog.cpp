// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "centered_checkbox_delegate.h"
#include "pkg_install_dir_select_dialog.h"
#include "pkg_install_model.h"

#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "gui_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

PkgInstallDirSelectDialog::PkgInstallDirSelectDialog(
    std::shared_ptr<EmulatorSettingsImpl> emu_settings, std::shared_ptr<GUISettings> gui_settings,
    QWidget* parent)
    : QDialog(parent), m_emu_settings(std::move(emu_settings)),
      m_gui_settings(std::move(gui_settings)) {

    auto* main_layout = new QVBoxLayout(this);

    auto* buttons = SetupDialogActions();
    auto* okButton = buttons->button(QDialogButtonBox::Ok);

    main_layout->addWidget(SetupGameSelectionTable());
    main_layout->addWidget(SetupInstallDirSelection(okButton));
    main_layout->addStretch();
    main_layout->addWidget(buttons);

    // DLC always installs to the addon (DLC) folder configured in emulator Settings -
    // never to the game install directory chosen below
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this, okButton] {
        UpdateInstallDirForSelection();
        UpdateOkButtonState(okButton);
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this, okButton] {
        UpdateInstallDirForSelection();
        UpdateOkButtonState(okButton);
    });

    setWindowTitle(tr("shadLauncher4 - Install PKG Files"));
    setWindowIcon(QIcon(":images/shadLauncher4.ico"));
    resize(700, 400);
}

PkgInstallDirSelectDialog::~PkgInstallDirSelectDialog() = default;

QWidget* PkgInstallDirSelectDialog::SetupGameSelectionTable() {
    auto* group = new QGroupBox(tr("Select PKG files to install"));
    auto* layout = new QVBoxLayout(group);

    m_model = new PkgInstallModel(this);

    m_game_view = new QTableView();
    m_game_view->setModel(m_model);
    m_game_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_game_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_game_view->verticalHeader()->setVisible(false);
    m_game_view->horizontalHeader()->setStretchLastSection(true);
    m_game_view->setAlternatingRowColors(true);
    m_game_view->setItemDelegateForColumn(PkgInstallModel::Install,
                                          new CenteredCheckboxDelegate(m_game_view));
    m_game_view->setColumnWidth(0, 60);
    m_game_view->setColumnWidth(1, 120);
    m_game_view->setColumnWidth(2, 260);
    m_game_view->setColumnWidth(3, 100);

    layout->addWidget(m_game_view);

    auto* btnLayout = new QHBoxLayout();
    auto* selectAll = new QPushButton(tr("Select All"));
    auto* selectNone = new QPushButton(tr("Select None"));
    auto* invert = new QPushButton(tr("Invert Selection"));

    btnLayout->addWidget(selectAll);
    btnLayout->addWidget(selectNone);
    btnLayout->addWidget(invert);
    btnLayout->addStretch();

    layout->addLayout(btnLayout);

    connect(selectAll, &QPushButton::clicked, this, [this]() {
        for (int r = 0; r < m_model->rowCount(); ++r)
            m_model->setData(m_model->index(r, 0), Qt::Checked, Qt::CheckStateRole);
    });

    connect(selectNone, &QPushButton::clicked, this, [this]() {
        for (int r = 0; r < m_model->rowCount(); ++r)
            m_model->setData(m_model->index(r, 0), Qt::Unchecked, Qt::CheckStateRole);
    });

    connect(invert, &QPushButton::clicked, this, [this]() {
        for (int r = 0; r < m_model->rowCount(); ++r) {
            const QModelIndex idx = m_model->index(r, 0);
            const bool checked = m_model->data(idx, Qt::CheckStateRole).toInt() == Qt::Checked;
            m_model->setData(idx, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        }
    });

    return group;
}

QWidget* PkgInstallDirSelectDialog::SetupInstallDirSelection(QPushButton* okButton) {
    auto* group = new QGroupBox(tr("Installation Directory"));
    auto* layout = new QVBoxLayout(group);

    auto* dirLayout = new QHBoxLayout();
    m_dir_combo = new QComboBox();
    m_browse_button = new QPushButton(tr("Browse..."));
    m_browse_button->setFixedWidth(80);

    dirLayout->addWidget(m_dir_combo);
    dirLayout->addWidget(m_browse_button);
    layout->addLayout(dirLayout);

    const auto& dirs = m_emu_settings->GetGameInstallDirs();
    for (const auto& dir : dirs) {
        QString qDir;
        Common::FS::PathToQString(qDir, dir);
        m_dir_combo->addItem(qDir);
    }

    if (!dirs.empty()) {
        // Prefer the last directory picked in this dialog (if it's still one of the
        // configured game install directories) over always defaulting to the first one.
        int initial_index = 0;
        if (m_gui_settings) {
            const QString last_dir =
                m_gui_settings->GetValue(GUI::general_last_pkg_install_dir).toString();
            if (!last_dir.isEmpty()) {
                const int found = m_dir_combo->findText(last_dir);
                if (found >= 0) {
                    initial_index = found;
                }
            }
        }
        m_dir_combo->setCurrentIndex(initial_index);
        SetSelectedDirectory(m_dir_combo->currentText());
    }

    auto* deleteCheck = new QCheckBox(tr("Delete PKG files after successful installation"));
    deleteCheck->setChecked(m_delete_file_on_install);
    layout->addWidget(deleteCheck);

    connect(deleteCheck, &QCheckBox::toggled, this,
            &PkgInstallDirSelectDialog::SetDeleteFileOnInstall);

    connect(m_dir_combo, &QComboBox::currentTextChanged, this,
            [this, okButton](const QString& text) {
                SetSelectedDirectory(text);
                UpdateOkButtonState(okButton);
            });

    connect(m_browse_button, &QPushButton::clicked, this, [this, okButton]() {
        QString current;
        Common::FS::PathToQString(current, m_selected_dir);

        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Directory"), current);

        if (dir.isEmpty())
            return;

        if (m_dir_combo->findText(dir) == -1) {
            m_dir_combo->addItem(dir);
            m_emu_settings->AddGameInstallDir(Common::FS::PathFromQString(dir));
            m_emu_settings->Save();
        }

        m_dir_combo->setCurrentText(dir);
        UpdateOkButtonState(okButton);
    });

    return group;
}

QDialogButtonBox* PkgInstallDirSelectDialog::SetupDialogActions() {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* okButton = buttons->button(QDialogButtonBox::Ok);

    okButton->setText(tr("Install Selected"));
    okButton->setEnabled(false);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!m_model->hasSelection()) {
            QMessageBox::warning(this, tr("No Selection"),
                                 tr("Please select at least one game to install."));
            return;
        }

        // DLC-only selections never need a game install directory
        if (!SelectionIsDlcOnly() && m_selected_dir.empty()) {
            QMessageBox::warning(this, tr("No Directory"),
                                 tr("Please select an installation directory."));
            return;
        }

        accept();
    });

    connect(buttons, &QDialogButtonBox::rejected, this, &PkgInstallDirSelectDialog::reject);

    return buttons;
}

void PkgInstallDirSelectDialog::SetPkgList(const std::vector<PkgInfo>& pkgs) {
    m_pkgs = pkgs;
    m_model->setPkgs(pkgs);

    if (auto* buttons = findChild<QDialogButtonBox*>()) {
        UpdateOkButtonState(buttons->button(QDialogButtonBox::Ok));
    }
}

std::vector<PkgInfo> PkgInstallDirSelectDialog::GetSelectedPkgs() const {
    return m_model->selectedPkgs();
}

void PkgInstallDirSelectDialog::SetSelectedDirectory(const QString& dir) {
    auto path = Common::FS::PathFromQString(dir);
    if (!path.empty() && std::filesystem::exists(path)) {
        m_selected_dir = path;
        if (!m_locked_for_dlc && m_gui_settings) {
            m_gui_settings->SetValue(GUI::general_last_pkg_install_dir, dir);
        }
    } else {
        m_selected_dir.clear();
    }
}

void PkgInstallDirSelectDialog::SetDeleteFileOnInstall(bool enabled) {
    m_delete_file_on_install = enabled;
}

void PkgInstallDirSelectDialog::UpdateOkButtonState(QPushButton* okButton) {
    const bool hasSelection = m_model && m_model->hasSelection();
    const bool hasDir = SelectionIsDlcOnly() || !m_selected_dir.empty();

    okButton->setEnabled(hasDir && hasSelection);
}

bool PkgInstallDirSelectDialog::SelectionIsDlcOnly() const {
    if (!m_model || !m_model->hasSelection()) {
        return false;
    }
    for (const auto& pkg : m_model->selectedPkgs()) {
        if (pkg.category != QStringLiteral("ac")) {
            return false;
        }
    }
    return true;
}

void PkgInstallDirSelectDialog::UpdateInstallDirForSelection() {
    if (!m_dir_combo || !m_browse_button) {
        return;
    }

    const bool dlcOnly = SelectionIsDlcOnly();

    if (dlcOnly && !m_locked_for_dlc) {
        m_locked_for_dlc = true;
        m_saved_dir_combo_index = m_dir_combo->currentIndex();

        QString addonDirQt;
        Common::FS::PathToQString(addonDirQt, m_emu_settings->GetAddonInstallDir());

        int idx = m_dir_combo->findText(addonDirQt);
        if (idx < 0) {
            m_dir_combo->addItem(addonDirQt);
            idx = m_dir_combo->count() - 1;
            m_dlc_addon_item_index = idx;
        } else {
            m_dlc_addon_item_index = -1; // already present - nothing to clean up later
        }
        m_dir_combo->setCurrentIndex(idx);

        m_dir_combo->setEnabled(false);
        m_browse_button->setEnabled(false);
    } else if (!dlcOnly && m_locked_for_dlc) {
        m_locked_for_dlc = false;
        m_dir_combo->setEnabled(true);
        m_browse_button->setEnabled(true);

        if (m_dlc_addon_item_index >= 0) {
            m_dir_combo->removeItem(m_dlc_addon_item_index);
            m_dlc_addon_item_index = -1;
        }
        if (m_saved_dir_combo_index >= 0 && m_saved_dir_combo_index < m_dir_combo->count()) {
            m_dir_combo->setCurrentIndex(m_saved_dir_combo_index);
        }
        m_saved_dir_combo_index = -1;
    }
}
