// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "centered_checkbox_delegate.h"
#include "pkg_install_dir_select_dialog.h"
#include "pkg_install_model.h"

#include "common/path_util.h"
#include "core/emulator_settings.h"

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

PkgInstallDirSelectDialog::PkgInstallDirSelectDialog(std::shared_ptr<EmulatorSettings> emu_settings,
                                                     QWidget* parent)
    : QDialog(parent), m_emu_settings(std::move(emu_settings)) {

    const auto install_dirs = m_emu_settings->GetGameInstallDirs();
    if (!install_dirs.empty())
        m_selected_dir = install_dirs.front();

    auto* main_layout = new QVBoxLayout(this);

    auto* buttons = SetupDialogActions();
    auto* okButton = buttons->button(QDialogButtonBox::Ok);

    main_layout->addWidget(SetupGameSelectionTable());
    main_layout->addWidget(SetupInstallDirSelection(okButton));
    main_layout->addStretch();
    main_layout->addWidget(buttons);

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
    auto* dirCombo = new QComboBox();
    auto* browseBtn = new QPushButton(tr("Browse..."));
    browseBtn->setFixedWidth(80);

    dirLayout->addWidget(dirCombo);
    dirLayout->addWidget(browseBtn);
    layout->addLayout(dirLayout);

    const auto& dirs = m_emu_settings->GetGameInstallDirs();
    for (const auto& dir : dirs) {
        QString qDir;
        Common::FS::PathToQString(qDir, dir);
        dirCombo->addItem(qDir);
    }

    if (!dirs.empty()) {
        dirCombo->setCurrentIndex(0);
        SetSelectedDirectory(dirCombo->currentText());
    }

    auto* deleteCheck = new QCheckBox(tr("Delete PKG files after successful installation"));
    deleteCheck->setChecked(m_delete_file_on_install);
    layout->addWidget(deleteCheck);

    connect(deleteCheck, &QCheckBox::toggled, this,
            &PkgInstallDirSelectDialog::SetDeleteFileOnInstall);

    connect(dirCombo, &QComboBox::currentTextChanged, this, [this, okButton](const QString& text) {
        SetSelectedDirectory(text);
        UpdateOkButtonState(okButton);
    });

    connect(browseBtn, &QPushButton::clicked, this, [this, dirCombo, okButton]() {
        QString current;
        Common::FS::PathToQString(current, m_selected_dir);

        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Directory"), current);

        if (dir.isEmpty())
            return;

        if (dirCombo->findText(dir) == -1) {
            dirCombo->addItem(dir);
            m_emu_settings->AddGameInstallDir(Common::FS::PathFromQString(dir));
            m_emu_settings->Save();
        }

        dirCombo->setCurrentText(dir);
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

        if (m_selected_dir.empty()) {
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
    if (!path.empty() && std::filesystem::exists(path))
        m_selected_dir = path;
    else
        m_selected_dir.clear();
}

void PkgInstallDirSelectDialog::SetDeleteFileOnInstall(bool enabled) {
    m_delete_file_on_install = enabled;
}

void PkgInstallDirSelectDialog::UpdateOkButtonState(QPushButton* okButton) {
    const bool hasDir = !m_selected_dir.empty();
    const bool hasSelection = m_model && m_model->hasSelection();
    okButton->setEnabled(hasDir && hasSelection);
}
