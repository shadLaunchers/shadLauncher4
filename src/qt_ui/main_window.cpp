// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QtConcurrent>
#include <common/string_util.h>
#include <common/versions.h>
#include <core/file_format/pkg.h>
#include <core/file_format/psf.h>

#include "background_music_player.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/loader.h"
#include "crypto_key_dialog.h"
#include "game_list_exporter.h"
#include "game_list_frame.h"
#include "gui_settings.h"
#include "main_window.h"
#include "pkg_install_dir_select_dialog.h"
#include "pkg_install_model.h"
#include "progress_dialog.h"
#include "settings_dialog.h"
#include "ui_main_window.h"
#include "user_manager_dialog.h"
#include "version.h"
#include "version_dialog.h"

MainWindow::MainWindow(std::shared_ptr<GUISettings> gui_settings,
                       std::shared_ptr<EmulatorSettings> emu_settings,
                       std::shared_ptr<PersistentSettings> persistent_settings,
                       std::shared_ptr<IpcClient> ipc_client, QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_gui_settings(gui_settings),
      m_emu_settings(std::move(emu_settings)), m_ipc_client(std::move(ipc_client)),
      m_persistent_settings(std::move(persistent_settings)) {

    Q_INIT_RESOURCE(shadLauncher4);
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);

    m_ipc_client->gameClosedFunc = [this]() { onGameClosed(); };
    m_ipc_client->restartEmulatorFunc = [this]() { RestartEmulator(); };
    m_ipc_client->startGameFunc = [this]() { RunGame(); };
}

MainWindow::~MainWindow() {}

bool MainWindow::init() {
    ui->toolBar->setObjectName("mw_toolbar");
    ui->sizeSlider->setRange(0, GUI::game_list_max_slider_pos);
    ui->toolBar->addWidget(ui->sizeSliderContainer);
    ui->toolBar->addWidget(ui->mw_searchbar);
    createActions();
    createDockWindows();
    createConnects();

    setMinimumSize(350, minimumSizeHint().height()); // seems fine on win 10
    setWindowTitle(QString("shadLauncher4 %1").arg(APP_VERSION));

    Q_EMIT RequestGlobalStylesheetChange();
    configureGuiFromSettings();

    show();
    // Refresh gamelist last
    m_game_list_frame->Refresh(true);
    m_game_list_frame->CheckCompatibilityAtStartup();

    LoadVersionComboBox();
    if (m_gui_settings->GetValue(GUI::version_manager_checkOnStartup).toBool()) {
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        versionDialog->checkUpdatePre(false);
    }

    // Expandable spacer to push elements to the right (Version Manager)
    QWidget* expandingSpacer = new QWidget(this);
    expandingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->toolBar->addWidget(expandingSpacer);
    QWidget* versionContainer = new QWidget(this);
    QVBoxLayout* versionLayout = new QVBoxLayout(versionContainer);
    versionLayout->setContentsMargins(0, 0, 0, 0);
    versionLayout->addWidget(ui->versionComboBox);
    versionLayout->addWidget(ui->versionManagerButton);
    ui->versionManagerButton->setText(tr("Version Manager"));
    ui->toolBar->addWidget(versionContainer);
    LoadVersionComboBox();

    return true;
}

void MainWindow::createActions() {
    ui->exitAct->setShortcuts(QKeySequence::Quit);

    m_icon_size_act_group = new QActionGroup(this);
    m_icon_size_act_group->addAction(ui->setIconSizeTinyAct);
    m_icon_size_act_group->addAction(ui->setIconSizeSmallAct);
    m_icon_size_act_group->addAction(ui->setIconSizeMediumAct);
    m_icon_size_act_group->addAction(ui->setIconSizeLargeAct);

    m_list_mode_act_group = new QActionGroup(this);
    m_list_mode_act_group->addAction(ui->setlistModeListAct);
    m_list_mode_act_group->addAction(ui->setlistModeGridAct);
}

void MainWindow::createConnects() {
    connect(m_icon_size_act_group, &QActionGroup::triggered, this, [this](QAction* act) {
        static const int index_small = GUI::GetIndex(GUI::game_list_icon_size_small);
        static const int index_medium = GUI::GetIndex(GUI::game_list_icon_size_medium);

        int index;

        if (act == ui->setIconSizeTinyAct)
            index = 0;
        else if (act == ui->setIconSizeSmallAct)
            index = index_small;
        else if (act == ui->setIconSizeMediumAct)
            index = index_medium;
        else
            index = GUI::game_list_max_slider_pos;

        m_save_slider_pos = true;
        resizeIcons(index);
    });
    connect(ui->showGameListAct, &QAction::triggered, this, [this](bool checked) {
        checked ? m_game_list_frame->show() : m_game_list_frame->hide();
        m_gui_settings->SetValue(GUI::main_window_gamelist, checked);
    });
    connect(ui->showTitleBarsAct, &QAction::triggered, this, [this](bool checked) {
        showTitleBars(checked);
        m_gui_settings->SetValue(GUI::main_window_titleBarsVisible, checked);
    });

    connect(ui->showToolBarAct, &QAction::triggered, this, [this](bool checked) {
        ui->toolBar->setVisible(checked);
        m_gui_settings->SetValue(GUI::main_window_toolBarVisible, checked);
    });

    connect(ui->showHiddenEntriesAct, &QAction::triggered, this, [this](bool checked) {
        m_gui_settings->SetValue(GUI::game_list_show_hidden, checked);
        m_game_list_frame->SetShowHidden(checked);
        m_game_list_frame->Refresh();
    });

    connect(ui->showLogAct, &QAction::triggered, this, [this](bool checked) {
        m_game_list_frame->ShowLog(checked);
        m_gui_settings->SetValue(GUI::main_window_showLog, checked);
    });

    connect(ui->showCompatibilityInGridAct, &QAction::triggered, m_game_list_frame,
            &GameListFrame::SetShowCompatibilityInGrid);

    connect(ui->refreshGameListAct, &QAction::triggered, this,
            [this] { m_game_list_frame->Refresh(true); });

    connect(m_game_list_frame, &GameListFrame::RequestIconSizeChange, this, [this](const int& val) {
        const int idx = ui->sizeSlider->value() + val;
        m_save_slider_pos = true;
        resizeIcons(idx);
    });
    connect(m_game_list_frame, &GameListFrame::GameListFrameClosed, this, [this]() {
        if (ui->showGameListAct->isChecked()) {
            ui->showGameListAct->setChecked(false);
            m_gui_settings->SetValue(GUI::main_window_gamelist, false);
        }
    });
    connect(m_list_mode_act_group, &QActionGroup::triggered, this, [this](QAction* act) {
        const bool is_list_act = act == ui->setlistModeListAct;
        if (is_list_act == m_is_list_mode)
            return;

        const int slider_pos = ui->sizeSlider->sliderPosition();
        ui->sizeSlider->setSliderPosition(m_other_slider_pos);
        setIconSizeActions(m_other_slider_pos);
        m_other_slider_pos = slider_pos;

        m_is_list_mode = is_list_act;
        m_game_list_frame->SetListMode(m_is_list_mode);
    });

    // toolbar actions
    connect(ui->toolbar_start, &QAction::triggered, this,
            [this] { MainWindow::StartGameWithArgs({}); });
    connect(ui->toolbar_stop, &QAction::triggered, this, &MainWindow::StopGame);
    connect(ui->toolbar_refresh, &QAction::triggered, this,
            [this]() { m_game_list_frame->Refresh(true); });
    connect(ui->toolbar_fullscreen, &QAction::triggered, this, &MainWindow::ToggleFullscreen);
    connect(ui->toolbar_list, &QAction::triggered, this,
            [this]() { ui->setlistModeListAct->trigger(); });
    connect(ui->toolbar_grid, &QAction::triggered, this,
            [this]() { ui->setlistModeGridAct->trigger(); });
    connect(ui->sizeSlider, &QSlider::valueChanged, this, &MainWindow::resizeIcons);
    connect(ui->sizeSlider, &QSlider::sliderReleased, this, [this] {
        const int index = ui->sizeSlider->value();
        m_gui_settings->SetValue(
            m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid, index);
        setIconSizeActions(index);
    });
    connect(ui->sizeSlider, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderNoAction &&
            action !=
                QAbstractSlider::SliderMove) { // we only want to save on mouseclicks or slider
                                               // release (the other connect handles this)
            m_save_slider_pos = true; // actionTriggered happens before the value was changed
        }
    });
    connect(ui->mw_searchbar, &QLineEdit::textChanged, m_game_list_frame,
            &GameListFrame::SetSearchText);
    connect(ui->mw_searchbar, &QLineEdit::returnPressed, m_game_list_frame,
            &GameListFrame::FocusAndSelectFirstEntryIfNoneIs);
    connect(m_game_list_frame, &GameListFrame::FocusToSearchBar, this,
            [this]() { ui->mw_searchbar->setFocus(); });

    connect(ui->actionManage_Users, &QAction::triggered, this, [this] {
        UserManagerDialog user_manager(m_gui_settings, m_emu_settings, this);
        user_manager.exec();
        m_game_list_frame->Refresh(true); // New user may have different games unlocked.
    });
    connect(ui->actionExport_GameList, &QAction::triggered, this, [this] {
        GameListExporter exporter(m_game_list_frame, this);
        exporter.ShowExportDialog();
    });

    const auto open_settings = [this](int tabIndex) {
        SettingsDialog* dlg =
            new SettingsDialog(m_gui_settings, m_emu_settings, m_ipc_client, tabIndex, this);

        connect(dlg, &SettingsDialog::GameFoldersChanged, this, [this]() {
            qDebug() << "Game folders changed!";
            m_game_list_frame->Refresh(true);
        });

        connect(dlg, &SettingsDialog::CompatUpdateRequested, m_game_list_frame, [this]() {
            if (m_game_list_frame) {
                m_game_list_frame->OnCompatUpdatedRequested();
            }
        });

        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    };
    connect(ui->actionConfigGeneral, &QAction::triggered, this,
            [open_settings]() { open_settings(0); });
    connect(ui->actionConfigGUI, &QAction::triggered, this,
            [open_settings]() { open_settings(1); });
    connect(ui->actionConfigGraphics, &QAction::triggered, this,
            [open_settings]() { open_settings(2); });
    connect(ui->actionConfigInput, &QAction::triggered, this,
            [open_settings]() { open_settings(3); });
    connect(ui->actionConfigPaths, &QAction::triggered, this,
            [open_settings]() { open_settings(4); });
    connect(ui->actionConfigLog, &QAction::triggered, this,
            [open_settings]() { open_settings(5); });
    connect(ui->actionConfigDebug, &QAction::triggered, this,
            [open_settings]() { open_settings(6); });
    connect(ui->actionConfigExperimental, &QAction::triggered, this,
            [open_settings]() { open_settings(7); });

    connect(ui->bootGameAct, &QAction::triggered, this,
            [this] { MainWindow::StartGameWithArgs({}); });
    connect(ui->sysPauseAct, &QAction::triggered, this, &MainWindow::PauseGame);
    connect(ui->sysRebootAct, &QAction::triggered, this, &MainWindow::RestartGame);
    connect(ui->sysStopAct, &QAction::triggered, this, &MainWindow::StopGame);

    connect(ui->toolbar_config, &QAction::triggered, this, [=]() { open_settings(0); });

    connect(ui->versionManagerButton, &QPushButton::clicked, this, [this]() {
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        connect(versionDialog, &QDialog::finished, this, [this](int) { LoadVersionComboBox(); });
        versionDialog->exec();
    });

    connect(ui->actionCrypto_Key_Manager, &QAction::triggered, this, [this] {
        CryptoManagerDialog dialog(this);
        dialog.exec();
    });

    connect(ui->install_pkg_act, &QAction::triggered, this, &MainWindow::InstallPkg);

    connect(this, &MainWindow::ExtractionFinished, this,
            [this]() { m_game_list_frame->Refresh(true); });

    connect(m_game_list_frame, &GameListFrame::RequestBoot, this,
            [this](game_info game) { StartGameWithArgs(game, {}); });

    connect(m_ipc_client.get(), &IpcClient::LogEntrySent, m_game_list_frame,
            &GameListFrame::PrintLog);
}

void MainWindow::LoadVersionComboBox() {
    ui->versionComboBox->clear();
    ui->versionComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    QString savedVersionPath =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    auto versions = VersionManager::GetVersionList();

    std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) {
        auto getOrder = [](int type) {
            switch (type) {
            case 1:
                return 0; // Pre-release
            case 0:
                return 1; // Release
            case 2:
                return 2; // Local
            default:
                return 3;
            }
        };

        int orderA = getOrder(static_cast<int>(a.type));
        int orderB = getOrder(static_cast<int>(b.type));
        if (orderA != orderB)
            return orderA < orderB;

        if (a.type == VersionManager::VersionType::Release) {
            static QRegularExpression versionRegex("^v\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$");
            QRegularExpressionMatch matchA = versionRegex.match(QString::fromStdString(a.name));
            QRegularExpressionMatch matchB = versionRegex.match(QString::fromStdString(b.name));

            if (matchA.hasMatch() && matchB.hasMatch()) {
                int majorA = matchA.captured(1).toInt();
                int minorA = matchA.captured(2).toInt();
                int patchA = matchA.captured(3).toInt();
                int majorB = matchB.captured(1).toInt();
                int minorB = matchB.captured(2).toInt();
                int patchB = matchB.captured(3).toInt();

                if (majorA != majorB)
                    return majorA > majorB;
                if (minorA != minorB)
                    return minorA > minorB;
                return patchA > patchB;
            }
        }

        return QString::fromStdString(a.name).compare(QString::fromStdString(b.name),
                                                      Qt::CaseInsensitive) < 0;
    });

    if (versions.empty()) {
        ui->versionComboBox->addItem(tr("None"));
        ui->versionComboBox->setCurrentIndex(0);
        return;
    }

    // Populate combo box
    for (const auto& v : versions) {
        ui->versionComboBox->addItem(QString::fromStdString(v.name),
                                     QString::fromStdString(v.path));
    }

    int selectedIndex = ui->versionComboBox->findData(savedVersionPath);
    ui->versionComboBox->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);

    // Disconnect previous connections to prevent duplicate execution
    ui->versionComboBox->disconnect();

    // Connect activated signal
    connect(
        ui->versionComboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            QString fullPath = ui->versionComboBox->itemData(index).toString();
            m_gui_settings->SetValue(GUI::version_manager_versionSelected, fullPath);

            QString rootFolder = QCoreApplication::applicationDirPath();
            QString destExe = rootFolder + "/shadPS4.exe";

            auto future = QtConcurrent::run([fullPath, destExe]() {
                if (QFile::exists(destExe))
                    QFile::remove(destExe);
                return QFile::copy(fullPath, destExe);
            });

            auto watcher = new QFutureWatcher<bool>();
            connect(watcher, &QFutureWatcher<bool>::finished, this, [watcher]() {
                bool success = watcher->result();
                watcher->deleteLater();

                if (success) {
                    QMessageBox::information(nullptr, QObject::tr("Version Activated"),
                                             QObject::tr("The selected version is now active."));
                } else {
                    QMessageBox::critical(nullptr, QObject::tr("Copy Failed"),
                                          QObject::tr("Unable to activate selected version."));
                }
            });
            watcher->setFuture(future);
        });

    ui->versionComboBox->adjustSize();
}

void MainWindow::createDockWindows() {
    // new mainwindow widget because existing seems to be bugged for now
    m_mw = new QMainWindow();
    m_mw->setContextMenuPolicy(Qt::PreventContextMenu);

    m_game_list_frame = new GameListFrame(m_gui_settings, m_emu_settings, m_persistent_settings,
                                          m_ipc_client, m_mw);
    m_game_list_frame->setObjectName("gamelist");

    m_mw->addDockWidget(Qt::LeftDockWidgetArea, m_game_list_frame);
    m_mw->setDockNestingEnabled(true);
    setCentralWidget(m_mw);
}

void MainWindow::updateLanguageActions(const QStringList& language_codes,
                                       const QString& language_code) {
    ui->languageMenu->clear();

    for (const auto& code : language_codes) {
        const QLocale locale(code);
        QString locale_name = locale.nativeLanguageName();

        if (locale.territory() != QLocale::AnyTerritory) {
            locale_name += " (" + locale.nativeTerritoryName() + ")";
        }

        // create new action
        QAction* act = new QAction(locale_name, this);
        act->setData(code);
        act->setToolTip(locale_name);
        act->setCheckable(true);
        act->setChecked(code == language_code);

        // connect to language changer
        connect(act, &QAction::triggered, this, [this, code]() { requestLanguageChange(code); });

        ui->languageMenu->addAction(act);
    }
}

void MainWindow::retranslateUI(const QStringList& language_codes, const QString& language_code) {
    updateLanguageActions(language_codes, language_code);

    ui->retranslateUi(this);

    if (m_game_list_frame) {
        m_game_list_frame->Refresh(true);
    }
}

void MainWindow::resizeIcons(int index) {
    if (ui->sizeSlider->value() != index) {
        ui->sizeSlider->setSliderPosition(index);
        return; // ResizeIcons will be triggered again by setSliderPosition, so return here
    }

    if (m_save_slider_pos) {
        m_save_slider_pos = false;
        m_gui_settings->SetValue(
            m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid, index);

        // this will also fire when we used the actions, but i didn't want to add another boolean
        // member
        setIconSizeActions(index);
    }

    m_game_list_frame->ResizeIcons(index);
}

void MainWindow::setIconSizeActions(int idx) const {
    static const int threshold_tiny =
        GUI::GetIndex((GUI::game_list_icon_size_small + GUI::game_list_icon_size_min) / 2);
    static const int threshold_small =
        GUI::GetIndex((GUI::game_list_icon_size_medium + GUI::game_list_icon_size_small) / 2);
    static const int threshold_medium =
        GUI::GetIndex((GUI::game_list_icon_size_max + GUI::game_list_icon_size_medium) / 2);

    if (idx < threshold_tiny)
        ui->setIconSizeTinyAct->setChecked(true);
    else if (idx < threshold_small)
        ui->setIconSizeSmallAct->setChecked(true);
    else if (idx < threshold_medium)
        ui->setIconSizeMediumAct->setChecked(true);
    else
        ui->setIconSizeLargeAct->setChecked(true);
}

void MainWindow::showTitleBars(bool show) const {
    m_game_list_frame->setTitleBarVisible(show);
}

void MainWindow::configureGuiFromSettings() {
    // Restore GUI state if needed. We need to if they exist.
    if (!restoreGeometry(m_gui_settings->GetValue(GUI::main_window_geometry).toByteArray())) {
        resize(QGuiApplication::primaryScreen()->availableSize() * 0.7);
    }

    restoreState(m_gui_settings->GetValue(GUI::main_window_windowState).toByteArray());
    m_mw->restoreState(m_gui_settings->GetValue(GUI::main_window_mwState).toByteArray());

    ui->showGameListAct->setChecked(m_gui_settings->GetValue(GUI::main_window_gamelist).toBool());
    ui->showToolBarAct->setChecked(
        m_gui_settings->GetValue(GUI::main_window_toolBarVisible).toBool());
    ui->showTitleBarsAct->setChecked(
        m_gui_settings->GetValue(GUI::main_window_titleBarsVisible).toBool());

    m_game_list_frame->setVisible(ui->showGameListAct->isChecked());
    ui->toolBar->setVisible(ui->showToolBarAct->isChecked());

    showTitleBars(ui->showTitleBarsAct->isChecked());

    ui->showHiddenEntriesAct->setChecked(
        m_gui_settings->GetValue(GUI::game_list_show_hidden).toBool());
    m_game_list_frame->SetShowHidden(
        ui->showHiddenEntriesAct
            ->isChecked()); // prevent GetValue in m_game_list_frame->LoadSettings

    ui->showLogAct->setChecked(m_gui_settings->GetValue(GUI::main_window_showLog).toBool());

    ui->showCompatibilityInGridAct->setChecked(
        m_gui_settings->GetValue(GUI::game_list_draw_compat).toBool());

    m_is_list_mode = m_gui_settings->GetValue(GUI::game_list_listMode).toBool();

    // handle icon size options
    if (m_is_list_mode)
        ui->setlistModeListAct->setChecked(true);
    else
        ui->setlistModeGridAct->setChecked(true);

    const int icon_size_index =
        m_gui_settings
            ->GetValue(m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid)
            .toInt();
    m_other_slider_pos =
        m_gui_settings
            ->GetValue(!m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid)
            .toInt();
    ui->sizeSlider->setSliderPosition(icon_size_index);
    setIconSizeActions(icon_size_index);

    // Gamelist
    m_game_list_frame->LoadSettings();
    BackgroundMusicPlayer::getInstance().SetVolume(
        m_gui_settings->GetValue(GUI::game_list_bg_volume).toInt());
}

void MainWindow::saveWindowState() const {
    // Save gui settings
    m_gui_settings->SetValue(GUI::main_window_geometry, saveGeometry(), false);
    m_gui_settings->SetValue(GUI::main_window_windowState, saveState(), false);

    if (m_mw) {
        m_gui_settings->SetValue(GUI::main_window_mwState, m_mw->saveState(), true);
    }

    if (m_game_list_frame) {
        // Save column settings
        m_game_list_frame->SaveSettings();
    }
}

void MainWindow::closeEvent(QCloseEvent* closeEvent) {
    saveWindowState();
}

void MainWindow::RepaintGUI() {
    if (m_game_list_frame) {
        m_game_list_frame->RepaintIcons(true);
    }
    // TODO finish this
}

void MainWindow::InstallPkg() {
    QFileDialog dialog;
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilter(tr("PKG File (*.PKG *.pkg)"));

    if (dialog.exec()) {
        QStringList fileNames = dialog.selectedFiles();
        if (fileNames.isEmpty()) {
            return;
        }

        // Convert QStringList to vector of filesystem paths
        std::vector<std::filesystem::path> pkgFiles;
        for (const QString& file : fileNames) {
            pkgFiles.push_back(Common::FS::PathFromQString(file));
        }

        // Use batch function - shows dialog once for all PKGs
        InstallDragDropPkgs(pkgFiles);
    }
}

static int PkgCategoryPriority(const QString& category) {
    const QString c = category.toLower();

    if (c.contains("game"))
        return 0; // base game
    if (c.contains("patch"))
        return 1;  // patch
    if (c == "ac") // DLC
        return 2;

    return 3; // unknown / others
}

static int CompareAppVersion(const QString& a, const QString& b) {
    const auto pa = a.split('.', Qt::SkipEmptyParts);
    const auto pb = b.split('.', Qt::SkipEmptyParts);

    const int maxParts = std::max(pa.size(), pb.size());

    for (int i = 0; i < maxParts; ++i) {
        int va = (i < pa.size()) ? pa[i].toInt() : 0;
        int vb = (i < pb.size()) ? pb[i].toInt() : 0;

        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

static void SortPkgsForInstall(std::vector<PkgInfo>& pkgs) {
    std::sort(pkgs.begin(), pkgs.end(), [](const PkgInfo& a, const PkgInfo& b) {
        // Group by title
        if (a.serial != b.serial)
            return a.serial < b.serial;

        // GAME then PATCH then DLC
        int pa = PkgCategoryPriority(a.category);
        int pb = PkgCategoryPriority(b.category);
        if (pa != pb)
            return pa < pb;

        // Version smaller to larger
        return CompareAppVersion(a.app_version, b.app_version) < 0;
    });
}

void MainWindow::InstallDragDropPkgs(const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    if (!KeyManager::GetInstance()->isPkgDerivedKey3KeysetValid()) {
        QMessageBox::critical(this, tr("PKG ERROR"),
                              tr("No valid PKG decryption keys found. Please set them up in the "
                                 "Crypto Key Manager."));
        return;
    }
    if (!KeyManager::GetInstance()->IsFakeKeysetValid()) {
        QMessageBox::critical(this, tr("PKG ERROR"),
                              tr("No valid Fake PKG decryption keys found. Please set them up in "
                                 "the Crypto Key Manager."));
        return;
    }

    // Validate all files are PKGs first
    std::vector<std::filesystem::path> validPkgFiles;
    for (const auto& file : files) {
        if (Loader::DetectFileType(file) != Loader::FileTypes::Pkg) {
            qWarning() << "Skipping non-PKG file:"
                       << QString::fromStdString(file.filename().string());
            continue;
        }
        validPkgFiles.push_back(file);
    }

    if (validPkgFiles.empty()) {
        QMessageBox::information(this, tr("PKG Install"), tr("No valid PKG files were found."));
        return;
    }

    // ---- Collect PKG info ----
    std::vector<PkgInfo> pkgInfos;
    pkgInfos.reserve(validPkgFiles.size());

    for (const auto& file : validPkgFiles) {
        std::string failreason;
        PKG pkg;
        if (!pkg.Open(file, failreason)) {
            QMessageBox::critical(this, tr("PKG ERROR"), QString::fromStdString(failreason));
            return;
        }

        PSF psf;
        if (!psf.Open(pkg.sfo)) {
            QMessageBox::critical(this, tr("PKG ERROR"),
                                  tr("Could not read SFO. Check log for details."));
            return;
        }

        auto titleView = psf.GetString("TITLE").value_or(std::string_view{});
        auto serialView = psf.GetString("TITLE_ID").value_or(std::string_view{});
        auto categoryView = psf.GetString("CATEGORY").value_or(std::string_view{});
        auto appVerView = psf.GetString("APP_VER").value_or(std::string_view{});

        PkgInfo info;
        info.title = QString::fromUtf8(titleView.data(), int(titleView.size()));
        info.serial = QString::fromUtf8(serialView.data(), int(serialView.size()));
        info.category = QString::fromUtf8(categoryView.data(), int(categoryView.size()));
        info.app_version = QString::fromUtf8(appVerView.data(), int(appVerView.size()));
        info.filepath = file;

        pkgInfos.push_back(std::move(info));
    }

    SortPkgsForInstall(pkgInfos);

    // Always show dialog for user to select which PKGs to install
    PkgInstallDirSelectDialog dialog(m_emu_settings, this);
    dialog.SetPkgList(pkgInfos);

    if (dialog.exec() == QDialog::Rejected) {
        return;
    }

    // Get user selections
    last_install_dir = dialog.GetSelectedDirectory();
    delete_file_on_install = dialog.GetDeleteFileOnInstall();

    auto selectedPkgs = dialog.GetSelectedPkgs();
    if (selectedPkgs.empty()) {
        return;
    }

    // Install each selected PKG
    for (size_t i = 0; i < selectedPkgs.size(); ++i) {
        InstallSinglePkg(selectedPkgs[i].filepath, static_cast<int>(i + 1),
                         static_cast<int>(selectedPkgs.size()));
    }
}

void MainWindow::InstallSinglePkg(std::filesystem::path file, int pkgNum, int nPkg) {
    if (Loader::DetectFileType(file) == Loader::FileTypes::Pkg) {
        std::string failreason;
        QElapsedTimer timer;
        timer.start();
        PKG pkg = PKG();
        PSF psf;
        if (!pkg.Open(file, failreason)) {
            QMessageBox::critical(this, tr("PKG ERROR"), QString::fromStdString(failreason));
            return;
        }
        if (!psf.Open(pkg.sfo)) {
            QMessageBox::critical(this, tr("PKG ERROR"),
                                  "Could not read SFO. Check log for details");
            return;
        }
        auto category = psf.GetString("CATEGORY");

        // No dialog logic here - all dialog logic is in InstallDragDropPkgs

        std::filesystem::path game_install_dir = last_install_dir;

        QString pkgType = QString::fromStdString(pkg.GetPkgFlags());
        bool use_game_update =
            pkgType.contains("PATCH") &&
            m_gui_settings->GetValue(GUI::general_separate_update_folder).toBool();

        // Default paths
        auto game_folder_path = game_install_dir / pkg.GetTitleID();
        auto game_update_path = use_game_update ? game_folder_path.parent_path() /
                                                      (std::string{pkg.GetTitleID()} + "-patch")
                                                : game_folder_path;
        const int max_depth = 5;

        if (pkgType.contains("PATCH")) {
            // For patches, try to find the game recursively
            auto found_game = Common::FS::FindGameByID(game_install_dir,
                                                       std::string{pkg.GetTitleID()}, max_depth);
            if (found_game.has_value()) {
                game_folder_path = found_game.value().parent_path();
                game_update_path = use_game_update ? game_folder_path.parent_path() /
                                                         (std::string{pkg.GetTitleID()} + "-patch")
                                                   : game_folder_path;
            }
        } else {
            // For base games, we check if the game is already installed
            auto found_game = Common::FS::FindGameByID(game_install_dir,
                                                       std::string{pkg.GetTitleID()}, max_depth);
            if (found_game.has_value()) {
                game_folder_path = found_game.value().parent_path();
            }
            // If the game is not found, we install it in the game install directory
            else {
                game_folder_path = game_install_dir / pkg.GetTitleID();
            }
            game_update_path = use_game_update ? game_folder_path.parent_path() /
                                                     (std::string{pkg.GetTitleID()} + "-patch")
                                               : game_folder_path;
        }

        QString gameDirPath;
        Common::FS::PathToQString(gameDirPath, game_folder_path);
        QDir game_dir(gameDirPath);
        if (game_dir.exists()) {
            QMessageBox msgBox;
            msgBox.setWindowTitle(tr("PKG Extraction"));

            std::string content_id;
            if (auto value = psf.GetString("CONTENT_ID"); value.has_value()) {
                content_id = std::string{*value};
            } else {
                QMessageBox::critical(this, tr("PKG ERROR"), "PSF file there is no CONTENT_ID");
                return;
            }
            std::string entitlement_label = Common::SplitString(content_id, '-')[2];

            auto addon_extract_path =
                m_emu_settings->GetAddonInstallDir() / pkg.GetTitleID() / entitlement_label;
            QString addonDirPath;
            Common::FS::PathToQString(addonDirPath, addon_extract_path);
            QDir addon_dir(addonDirPath);

            if (pkgType.contains("PATCH")) {
                QString pkg_app_version;
                if (auto app_ver = psf.GetString("APP_VER"); app_ver.has_value()) {
                    pkg_app_version = QString::fromStdString(std::string{*app_ver});
                } else {
                    QMessageBox::critical(this, tr("PKG ERROR"), "PSF file there is no APP_VER");
                    return;
                }
                std::filesystem::path sce_folder_path =
                    std::filesystem::exists(game_update_path / "sce_sys" / "param.sfo")
                        ? game_update_path / "sce_sys" / "param.sfo"
                        : game_folder_path / "sce_sys" / "param.sfo";
                psf.Open(sce_folder_path);
                QString game_app_version;
                if (auto app_ver = psf.GetString("APP_VER"); app_ver.has_value()) {
                    game_app_version = QString::fromStdString(std::string{*app_ver});
                } else {
                    QMessageBox::critical(this, tr("PKG ERROR"), "PSF file there is no APP_VER");
                    return;
                }
                double appD = game_app_version.toDouble();
                double pkgD = pkg_app_version.toDouble();
                if (pkgD == appD) {
                    msgBox.setText(QString(tr("Patch detected!") + "\n" +
                                           tr("PKG and Game versions match: ") + pkg_app_version +
                                           "\n" + tr("Would you like to overwrite?")));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                } else if (pkgD < appD) {
                    msgBox.setText(QString(tr("Patch detected!") + "\n" +
                                           tr("PKG Version %1 is older than installed version: ")
                                               .arg(pkg_app_version) +
                                           game_app_version + "\n" +
                                           tr("Would you like to overwrite?")));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                } else {
                    msgBox.setText(QString(tr("Patch detected!") + "\n" +
                                           tr("Game is installed: ") + game_app_version + "\n" +
                                           tr("Would you like to install Patch: ") +
                                           pkg_app_version + " ?"));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                }
                int result = msgBox.exec();
                if (result == QMessageBox::Yes) {
                    // Do nothing.
                } else {
                    return;
                }
            } else if (category == "ac") {
                if (!addon_dir.exists()) {
                    QMessageBox addonMsgBox;
                    addonMsgBox.setWindowTitle(tr("DLC Installation"));
                    addonMsgBox.setText(QString(tr("Would you like to install DLC: %1?"))
                                            .arg(QString::fromStdString(entitlement_label)));

                    addonMsgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    addonMsgBox.setDefaultButton(QMessageBox::No);
                    int result = addonMsgBox.exec();
                    if (result == QMessageBox::Yes) {
                        game_update_path = addon_extract_path;
                    } else {
                        return;
                    }
                } else {
                    msgBox.setText(QString(tr("DLC already installed:") + "\n" + addonDirPath +
                                           "\n\n" + tr("Would you like to overwrite?")));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                    int result = msgBox.exec();
                    if (result == QMessageBox::Yes) {
                        game_update_path = addon_extract_path;
                    } else {
                        return;
                    }
                }
            } else {
                msgBox.setText(QString(tr("Game already installed") + "\n" + gameDirPath + "\n" +
                                       tr("Would you like to overwrite?")));
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);
                int result = msgBox.exec();
                if (result == QMessageBox::Yes) {
                    // Do nothing.
                } else {
                    return;
                }
            }
        } else {
            // Do nothing;
            if (pkgType.contains("PATCH") || category == "ac") {
                QMessageBox::information(
                    this, tr("PKG Extraction"),
                    tr("PKG is a patch or DLC, please install the game first!"));
                return;
            }
            // what else?
        }
        if (!pkg.Extract(file, game_update_path, failreason)) {
            QMessageBox::critical(this, tr("PKG ERROR"), QString::fromStdString(failreason));
        } else {
            int nfiles = pkg.GetNumberOfFiles();

            if (nfiles > 0) {
                QVector<int> indices;
                for (int i = 0; i < nfiles; i++) {
                    indices.append(i);
                }

                QProgressDialog dialog;
                dialog.setWindowTitle(tr("PKG Extraction"));
                dialog.setWindowModality(Qt::WindowModal);
                QString extractmsg = QString(tr("Extracting PKG %1/%2")).arg(pkgNum).arg(nPkg);
                dialog.setLabelText(extractmsg);
                dialog.setAutoClose(true);
                dialog.setRange(0, nfiles);

                dialog.setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                                       dialog.size(), this->geometry()));

                QFutureWatcher<void> futureWatcher;
                connect(&futureWatcher, &QFutureWatcher<void>::finished, this, [=, this]() {
                    qint64 elapsed = timer.elapsed(); // milliseconds
                    qDebug() << "Total extraction took:" << elapsed << "ms (" << elapsed / 1000.0
                             << "s)"; // TODO to be removed

                    if (pkgNum == nPkg) {
                        QString path;

                        // We want to show the parent path instead of the full path
                        Common::FS::PathToQString(path, game_folder_path.parent_path());
                        QIcon windowIcon(
                            Common::FS::PathToUTF8String(game_folder_path / "sce_sys/icon0.png")
                                .c_str());

                        QMessageBox extractMsgBox(this);
                        extractMsgBox.setWindowTitle(tr("Extraction Finished"));
                        if (!windowIcon.isNull()) {
                            extractMsgBox.setWindowIcon(windowIcon);
                        }
                        extractMsgBox.setText(
                            QString(tr("Game successfully installed at %1")).arg(path));
                        extractMsgBox.addButton(QMessageBox::Ok);
                        extractMsgBox.setDefaultButton(QMessageBox::Ok);
                        connect(&extractMsgBox, &QMessageBox::buttonClicked, this,
                                [&](QAbstractButton* button) {
                                    if (extractMsgBox.button(QMessageBox::Ok) == button) {
                                        extractMsgBox.close();
                                        emit ExtractionFinished();
                                    }
                                });
                        extractMsgBox.exec();
                    }
                    if (delete_file_on_install) {
                        std::filesystem::remove(file);
                    }
                });
                connect(&dialog, &QProgressDialog::canceled, [&]() { futureWatcher.cancel(); });
                connect(&futureWatcher, &QFutureWatcher<void>::progressValueChanged, &dialog,
                        &QProgressDialog::setValue);
                futureWatcher.setFuture(
                    QtConcurrent::map(indices, [&](int index) { pkg.ExtractFiles(index); }));
                dialog.exec();
            }
        }
    } else {
        QMessageBox::critical(this, tr("PKG ERROR"),
                              tr("File doesn't appear to be a valid PKG file"));
    }
}

void MainWindow::StartGameWithArgs(const game_info& game, QStringList args) {
    BackgroundMusicPlayer::getInstance().StopMusic();
    QString gamePath = "";
    game_info selected_game_info;

    if (!game) {
        selected_game_info = m_game_list_frame->GetSelectedGameInfo();
        if (!selected_game_info) {
            QMessageBox::information(this, tr("Error"), tr("No game selected"));
            return;
        }
    } else {
        selected_game_info = game;
    }

    std::filesystem::path basePath = selected_game_info->info.path;
    std::filesystem::path ebootPath = basePath / "eboot.bin";
    Common::FS::PathToQString(gamePath, ebootPath);

    if (gamePath != "") {
        // AddRecentFiles(gamePath);
        if (!std::filesystem::exists(ebootPath)) {
            QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Eboot.bin file not found")));
            return;
        }
        StartEmulator(ebootPath, args);
        last_game_info = selected_game_info;

        // UpdateToolbarButtons();
    }
}

void MainWindow::StartEmulator(std::filesystem::path path, QStringList args) {
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Game is already running!")));
        return;
    }

    QString selectedVersion =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    if (selectedVersion.isEmpty()) {
        QMessageBox::warning(this, tr("No Version Selected"),
                             // clang-format off
                             tr("No emulator version was selected.\nThe Version Manager menu will then open.\nSelect an emulator version from the right panel."));
        // clang-format on
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        connect(versionDialog, &QDialog::finished, this, [this](int) { LoadVersionComboBox(); });
        versionDialog->exec();
        return;
    }

    QFileInfo fileInfo(selectedVersion);
    if (!fileInfo.exists()) {
        QMessageBox::critical(nullptr, "shadPS4",
                              QString(tr("Could not find the emulator executable")));
        return;
    }

    QStringList final_args{"--game", QString::fromStdWString(path.wstring())};
    final_args.append(args);

    EmulatorState::GetInstance()->SetGameRunning(true);

    QString workDir = QDir::currentPath();
    m_ipc_client->startEmulator(fileInfo, final_args, workDir);
    // m_ipc_client->setActiveController(GamepadSelect::GetSelectedGamepad());
}

void MainWindow::RunGame() {
    auto info = last_game_info;
    auto appVersion = info->info.app_ver;
    auto gameSerial = info->info.serial;
    auto patches = MemoryPatcher::readPatches(gameSerial, appVersion);
    for (auto patch : patches) {
        m_ipc_client->sendMemoryPatches(patch.modName, patch.address, patch.value, patch.target,
                                        patch.size, patch.maskOffset, patch.littleEndian,
                                        patch.mask, patch.maskOffset);
    }

    m_ipc_client->startGame();
}

void MainWindow::onGameClosed() {
    EmulatorState::GetInstance()->SetGameRunning(false);
    is_paused = false;

    /* TODO
    // clear dialogs when game closed
    skylander_dialog* sky_diag = skylander_dialog::get_dlg(this, m_ipc_client);
    sky_diag->clear_all();
    dimensions_dialog* dim_diag = dimensions_dialog::get_dlg(this, m_ipc_client);
    dim_diag->clear_all();
    infinity_dialog* inf_diag = infinity_dialog::get_dlg(this, m_ipc_client);
    inf_diag->clear_all();
    */
}

void MainWindow::RestartEmulator() {
    QString exe = m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    std::filesystem::path last_game_path = last_game_info->info.path;
    QStringList args{"--game", QString::fromStdWString(last_game_path.wstring())};

    if (m_ipc_client->parsedArgs.size() > 0) {
        args.clear();
        for (auto arg : m_ipc_client->parsedArgs) {
            args.append(QString::fromStdString(arg));
        }
        m_ipc_client->parsedArgs.clear();
    }

    QFileInfo fileInfo(exe);
    QString workDir = QDir::currentPath();

    m_ipc_client->startEmulator(fileInfo, args, workDir);
}

void MainWindow::RestartGame() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to restart")));
        return;
    }

    m_ipc_client->restartEmulator();
}

void MainWindow::PauseGame() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to pause")));
        return;
    }

    if (is_paused) {
        m_ipc_client->resumeGame();
        ui->sysPauseAct->setText(tr("Pause"));
        ui->sysPauseAct->setToolTip(tr("Pause emulation"));
        is_paused = false;
    } else {
        m_ipc_client->pauseGame();
        ui->sysPauseAct->setText(tr("Resume"));
        ui->sysPauseAct->setToolTip(tr("Resume emulation"));
        is_paused = true;
    }
}

void MainWindow::StopGame() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to stop")));
        return;
    }

    m_ipc_client->stopEmulator();
}

void MainWindow::ToggleFullscreen() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to toggle fullscreen")));
        return;
    }

    m_ipc_client->toggleFullscreen();
}
