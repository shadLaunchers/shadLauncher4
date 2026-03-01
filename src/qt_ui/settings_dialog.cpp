// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDesktopServices>
#include <QtWidgets>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include "VulkanDeviceLib.h"
#include "background_music_player.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "game_info.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "log_presets_dialog.h"
#include "settings_dialog.h"
#include "settings_dialog_helper_texts.h"
#include "ui_settings_dialog.h"

// Normalize paths consistently for equality checks
static inline std::string NormalizePath(const std::filesystem::path& p) {
    // Convert to a normalized lexical path
    auto np = p.lexically_normal();

    // Convert to UTF-8 string
    auto u8 = np.generic_u8string();
    std::string s(u8.begin(), u8.end());

#ifdef _WIN32
    // Windows paths: drive letters are case-insensitive to normalize case
    // Example: "C:/Games" vs "c:/Games"
    if (s.size() >= 2 && s[1] == ':')
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
#endif

    return s;
}

// Equality operators
inline bool operator==(GameInstallDir const& a, GameInstallDir const& b) {
    return a.enabled == b.enabled && NormalizePath(a.path) == NormalizePath(b.path);
}

inline bool operator!=(GameInstallDir const& a, GameInstallDir const& b) {
    return !(a == b);
}

SettingsDialog::SettingsDialog(std::shared_ptr<GUISettings> gui_settings,
                               std::shared_ptr<EmulatorSettings> emu_settings,
                               std::shared_ptr<IpcClient> ipc_client, int tab_index,
                               QWidget* parent, const GameInfo* game, bool customFromGlobal)
    : QDialog(parent), m_tab_index(tab_index), ui(new Ui::SettingsDialog),
      m_gui_settings(std::move(gui_settings)), m_emu_settings(std::move(emu_settings)),
      m_ipc_client(ipc_client), m_custom_settings_from_global(customFromGlobal) {
    ui->setupUi(this);

    // Store game info if provided
    if (game) {
        m_current_game = *game;
        m_game_serial = game->serial;
    }

    if (!IsGlobal() && m_custom_settings_from_global && !m_game_serial.empty()) {
        // We need to load game-specific settings
        m_original_settings = std::make_shared<EmulatorSettings>();
        *m_original_settings = *m_emu_settings; // Backup original

        // Create and load game-specific settings
        m_game_specific_settings = std::make_shared<EmulatorSettings>();
        m_game_specific_settings->Load("");            // Load global
        m_game_specific_settings->Load(m_game_serial); // Apply overrides

        // Use game-specific settings
        m_emu_settings.swap(m_game_specific_settings);

        this->setWindowTitle(tr("Custom Settings for %1 [%2]")
                                 .arg(QString::fromStdString(m_current_game.name),
                                      QString::fromStdString(m_game_serial)));
        ui->customSettingsLabel->setVisible(true);
    } else if (IsGlobal()) {
        this->setWindowTitle(tr("Global Settings"));
        ui->customSettingsLabel->setVisible(false);
        int index = ui->tabWidgetSettings->indexOf(ui->experimentalTab);
        ui->tabWidgetSettings->setTabVisible(index, false);
    }

    const SettingsDialogHelperTexts helptexts;
    SubscribeHelpText(ui->gameFoldersGroupBox, helptexts.settings.paths_gameDir);
    SubscribeHelpText(ui->gameFoldersListWidget, helptexts.settings.paths_gameDir);
    SubscribeHelpText(ui->addFolderButton, helptexts.settings.paths_gameDir_add);
    SubscribeHelpText(ui->removeFolderButton, helptexts.settings.paths_gameDir_remove);
    SubscribeHelpText(ui->dlcFolderGroupBox, helptexts.settings.paths_dlcDir);
    SubscribeHelpText(ui->currentDLCFolder, helptexts.settings.paths_dlcDir);
    SubscribeHelpText(ui->browseDLCButton, helptexts.settings.paths_dlcDir_browse);
    SubscribeHelpText(ui->homeGroupBox, helptexts.settings.paths_homeDir);
    SubscribeHelpText(ui->currentHomePath, helptexts.settings.paths_homeDir);
    SubscribeHelpText(ui->browseHomeButton, helptexts.settings.paths_homeDir_browse);
    SubscribeHelpText(ui->sysmodulesGroupBox, helptexts.settings.paths_homeDir);
    SubscribeHelpText(ui->currentSysmodulesPath, helptexts.settings.paths_sysmodulesDir);
    SubscribeHelpText(ui->browseSysmodulesButton, helptexts.settings.paths_sysmodulesDir_browse);
    SubscribeHelpText(ui->ScanDepthComboBox, helptexts.settings.general_scan_depth_combo);
    SubscribeHelpText(ui->showSplashCheckBox, helptexts.settings.general_show_splash);
    SubscribeHelpText(ui->horizontalVolumeSlider, helptexts.settings.general_volume_slider);
    SubscribeHelpText(ui->disableTrophycheckBox, helptexts.settings.general_disable_trophy_popup);
    SubscribeHelpText(ui->currentFontsPath, helptexts.settings.paths_fontsDir);
    SubscribeHelpText(ui->browseFontsButton, helptexts.settings.paths_fontsDir_browse);

    PopulateComboBoxes();
    PathTabConnections();
    OtherConnections();

    if (!IsGlobal()) {
        MapUIControls();
        DisableNonOverrideableSettings();
    }

    LoadValuesFromConfig();

    HandleButtonBox();
}

SettingsDialog::~SettingsDialog() {
    // Clean up game-specific settings when dialog closes
    if (m_game_specific_settings) {
        // If we swapped settings, swap them back
        if (!IsGlobal() && m_custom_settings_from_global) {
            if (m_original_settings) {
                // Restore original settings
                m_emu_settings.swap(m_game_specific_settings);
            }
        }

        // Clear the shared_ptr
        m_game_specific_settings.reset();
    }

    // Also clear original settings
    if (m_original_settings) {
        m_original_settings.reset();
    }
}

void SettingsDialog::open() {
    QDialog::open();
    ui->tabWidgetSettings->setCurrentIndex(m_tab_index);
}

// ---------------------------- Help text ----------------------------
void SettingsDialog::SubscribeHelpText(QObject* object, const QString& text) {
    m_descriptions[object] = text;
    object->installEventFilter(this);
}

bool SettingsDialog::eventFilter(QObject* object, QEvent* event) {
    if (!m_descriptions.contains(object))
        return QDialog::eventFilter(object, event);

    if (event->type() == QEvent::Enter) {
        ui->descriptionText->setText(m_descriptions[object].replace("\\n", "\n"));
    } else if (event->type() == QEvent::Leave) {
        ui->descriptionText->setText(
            tr("Point your mouse at an option to display its description."));
    }

    return QDialog::eventFilter(object, event);
}

// ---------------------------- Path Tab Connections (UI only) ----------------------------
void SettingsDialog::PathTabConnections() {
    // -------------- Games Folder --------------------------------------------------------
    auto* list = ui->gameFoldersListWidget;

    // Enable drag & drop internal reordering (UI only)
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDragEnabled(true);
    list->setAcceptDrops(true);
    list->setDropIndicatorShown(true);

    // --- Add folder (UI only) ---
    connect(ui->addFolderButton, &QPushButton::clicked, this, [this]() {
        const QString sel =
            QFileDialog::getExistingDirectory(this, tr("Directory to install games"));
        if (sel.isEmpty())
            return;

        const auto path = Common::FS::PathFromQString(sel);
        if (!std::filesystem::exists(path)) {
            QMessageBox::warning(this, tr("Invalid Path"), tr("Selected folder does not exist."));
            return;
        }

        // Prevent duplicates by comparing raw text entries (UI-level)
        for (int i = 0; i < ui->gameFoldersListWidget->count(); ++i) {
            if (ui->gameFoldersListWidget->item(i)->text() == sel) {
                QMessageBox::warning(this, tr("Duplicate Path"),
                                     tr("This folder is already added."));
                return;
            }
        }

        auto* item = new QListWidgetItem(sel);
        item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        ui->gameFoldersListWidget->addItem(item);
    });

    // --- Remove folder (UI only) ---
    connect(ui->removeFolderButton, &QPushButton::clicked, this, [this]() {
        auto* item = ui->gameFoldersListWidget->currentItem();
        if (item)
            delete item;
    });

    // Enable/disable remove button depending on selection
    connect(list, &QListWidget::itemSelectionChanged, this, [this]() {
        ui->removeFolderButton->setEnabled(!ui->gameFoldersListWidget->selectedItems().isEmpty());
    });

    // --- Double-click opens folder ---
    connect(list, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        if (item)
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
    });

    // --- Context menu (UI only): open / remove / toggle ---
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = ui->gameFoldersListWidget->itemAt(pos);
        if (!item)
            return;

        QMenu menu(this);
        QAction* openAction = menu.addAction(tr("Open Folder"));
        QAction* removeAction = menu.addAction(tr("Remove"));
        QAction* toggleAction =
            menu.addAction(item->checkState() == Qt::Checked ? tr("Disable") : tr("Enable"));

        QAction* chosen = menu.exec(ui->gameFoldersListWidget->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == openAction) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
        } else if (chosen == removeAction) {
            delete item;
        } else if (chosen == toggleAction) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    });

    // ------------------Addon Folder ----------------------------------------------------------
    connect(ui->browseDLCButton, &QPushButton::clicked, this, [this]() {
        const auto dlc_folder_path = m_emu_settings->GetAddonInstallDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, dlc_folder_path);

        QString dlc_folder_path_string =
            QFileDialog::getExistingDirectory(this, tr("Select directory for DLC"), initial_path);

        auto file_path = Common::FS::PathFromQString(dlc_folder_path_string);
        if (!file_path.empty()) {
            ui->currentDLCFolder->setText(dlc_folder_path_string);
        }
    });
    // --------------Home Folder --------------------------------------------------------
    connect(ui->browseHomeButton, &QPushButton::clicked, this, [this]() {
        const auto home_path = m_emu_settings->GetHomeDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, home_path);

        QString home_path_string =
            QFileDialog::getExistingDirectory(this, tr("Select directory for home"), initial_path);

        auto file_path = Common::FS::PathFromQString(home_path_string);
        if (!file_path.empty()) {
            ui->currentHomePath->setText(home_path_string);
        }
    });
    // -----------Sys Modules Folder --------------------------------------------------------------
    connect(ui->browseSysmodulesButton, &QPushButton::clicked, this, [this]() {
        const auto sysmodules_path = m_emu_settings->GetSysModulesDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, sysmodules_path);

        QString sysmodules_path_string = QFileDialog::getExistingDirectory(
            this, tr("Select directory for System modules"), initial_path);

        auto file_path = Common::FS::PathFromQString(sysmodules_path_string);
        if (!file_path.empty()) {
            ui->currentSysmodulesPath->setText(sysmodules_path_string);
        }
    });
    // -----------Fonts Folder --------------------------------------------------------------
    connect(ui->browseFontsButton, &QPushButton::clicked, this, [this]() {
        const auto fonts_path = m_emu_settings->GetFontsDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, fonts_path);

        QString fonts_path_string = QFileDialog::getExistingDirectory(
            this, tr("Select directory for System fonts"), initial_path);

        auto file_path = Common::FS::PathFromQString(fonts_path_string);
        if (!file_path.empty()) {
            ui->currentFontsPath->setText(fonts_path_string);
        }
    });
}

// ---------------------------- Non-Path Connections ----------------------------
void SettingsDialog::OtherConnections() {

    // ------------------ General tab --------------------------------------------------------
    connect(ui->horizontalVolumeSlider, &QSlider::valueChanged, [this](int value) {
        ui->volumeText->setText(QString("%1%").arg(value));

        if (EmulatorState::GetInstance()->IsGameRunning())
            m_ipc_client->adjustVol(value, !IsGlobal());
    });

    connect(ui->OpenCustomTrophyLocationButton, &QPushButton::clicked, this, []() {
        QString userPath;
        Common::FS::PathToQString(userPath,
                                  Common::FS::GetUserPath(Common::FS::PathType::CustomTrophy));

        if (!QDir().exists(userPath))
            QDir().mkpath(userPath);

        QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
    });

    // ------------------ Gui tab --------------------------------------------------------
    connect(ui->BGMVolumeSlider, &QSlider::valueChanged, this,
            [](int value) { BackgroundMusicPlayer::getInstance().SetVolume(value); });

    // ------------------ Graphics tab --------------------------------------------------------
    connect(ui->RCASSlider, &QSlider::valueChanged, [this](int value) {
        QString RCASValue = QString::number(value / 1000.0, 'f', 3);
        ui->RCASValue->setText(RCASValue);
    });

    if (EmulatorState::GetInstance()->IsGameRunning()) {
        connect(ui->RCASSlider, &QSlider::valueChanged, this,
                [this](int value) { m_ipc_client->setRcasAttenuation(value); });
        connect(ui->FSRCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) { m_ipc_client->setFsr(state); });
        connect(ui->RCASCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) { m_ipc_client->setRcas(state); });
    }

    // ------------------ Input tab --------------------------------------------------------
    connect(ui->hideCursorComboBox, &QComboBox::currentTextChanged, this, [this](QString text) {
        if (text == tr("Idle")) {
            ui->idleTimeoutGroupBox->show();
        } else {
            ui->idleTimeoutGroupBox->hide();
        }
    });

    // ------------------ Log tab --------------------------------------------------------
    connect(ui->OpenLogLocationButton, &QPushButton::clicked, this, []() {
        QString userPath;
        Common::FS::PathToQString(userPath, Common::FS::GetUserPath(Common::FS::PathType::LogDir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
    });

    connect(ui->logPresetsButton, &QPushButton::clicked, this, [this]() {
        auto dlg = new LogPresetsDialog(m_gui_settings, this);
        connect(dlg, &LogPresetsDialog::PresetChosen, this,
                [this](const QString& filter) { ui->logFilterLineEdit->setText(filter); });
        dlg->exec();
    });

    // ------------------ Debug --------------------------------------------------------
    connect(ui->vkValidationCheckBox, &QCheckBox::checkStateChanged, this,
            [this](Qt::CheckState state) {
                state ? ui->vkLayersGroupBox->setVisible(true)
                      : ui->vkLayersGroupBox->setVisible(false);
            });
}

// ---------------------------- Load from backend to UI ----------------------------
void SettingsDialog::LoadValuesFromConfig() {

    // ------------------ General tab --------------------------------------------------------
    ui->showSplashCheckBox->setChecked(m_emu_settings->IsShowSplash());
    ui->horizontalVolumeSlider->setValue(m_emu_settings->GetVolumeSlider());
    ui->GenAudioComboBox->setCurrentText(
        QString::fromStdString(m_emu_settings->GetMainOutputDevice()));
    ui->DsAudioComboBox->setCurrentText(
        QString::fromStdString(m_emu_settings->GetPadSpkOutputDevice()));
    ui->disableTrophycheckBox->setChecked(m_emu_settings->IsTrophyPopupDisabled());
    ui->popUpDurationSpinBox->setValue(m_emu_settings->GetTrophyNotificationDuration());

    QString trophy_side = QString::fromStdString(m_emu_settings->GetTrophyNotificationSide());
    ui->radioButton_Left->setChecked(trophy_side == "left");
    ui->radioButton_Right->setChecked(trophy_side == "right");
    ui->radioButton_Top->setChecked(trophy_side == "top");
    ui->radioButton_Bottom->setChecked(trophy_side == "bottom");
    ui->showFpsCounterCheckBox->setChecked(m_emu_settings->IsShowFpsCounter());

    // ------------------ GUI tab --------------------------------------------------------
    ui->discordRPCCheckbox->setChecked(m_emu_settings->IsDiscordRPCEnabled());
    ui->playBGMCheckBox->setChecked(m_gui_settings->GetValue(GUI::game_list_play_bg).toBool());
    ui->BGMVolumeSlider->setValue(m_gui_settings->GetValue(GUI::game_list_bg_volume).toInt());
    ui->showBackgroundImageCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::game_list_showBackgroundImage).toBool());
    ui->backgroundImageOpacitySlider->setValue(
        m_gui_settings->GetValue(GUI::game_list_backgroundImageOpacity).toInt());
    ui->checkCompatibilityOnStartupCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::compatibility_check_on_startup).toBool());
    ui->updaterCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::general_check_gui_updates).toBool());
    ui->changelogCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::general_show_changelog).toBool());
    ui->separateUpdateCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::general_separate_update_folder).toBool());

    // ------------------ Graphics tab --------------------------------------------------------
    // First options is auto selection -1, so gpuId on the GUI will always have to subtract 1
    // when setting and add 1 when getting to select the correct gpu in Qt
    ui->graphicsAdapterBox->setCurrentIndex(m_emu_settings->GetGpuId() + 1);

    std::string presentMode = m_emu_settings->GetPresentMode();
    QString translatedText_PresentMode = presentModeMap.key(QString::fromStdString(presentMode));
    ui->presentModeComboBox->setCurrentText(translatedText_PresentMode);

    std::string fullscreenMode = m_emu_settings->GetFullScreenMode();
    QString translatedText_FullscreenMode =
        screenModeMap.key(QString::fromStdString(fullscreenMode));
    ui->displayModeComboBox->setCurrentText(translatedText_FullscreenMode);

    ui->nullGpuCheckBox->setChecked(m_emu_settings->IsNullGPU());
    ui->heightSpinBox->setValue(m_emu_settings->GetWindowHeight());
    ui->widthSpinBox->setValue(m_emu_settings->GetWindowWidth());
    ui->enableHDRCheckBox->setChecked(m_emu_settings->IsHdrAllowed());

    ui->FSRCheckBox->setChecked(m_emu_settings->IsFsrEnabled());
    ui->RCASCheckBox->setChecked(m_emu_settings->IsRcasEnabled());
    ui->RCASSlider->setValue(m_emu_settings->GetRcasAttenuation());

    // ------------------ Input tab --------------------------------------------------------
    HideCursorState cursorState = static_cast<HideCursorState>(m_emu_settings->GetCursorState());
    QString translatedText_cursorState = cursorStateMap.key(cursorState);
    ui->hideCursorComboBox->setCurrentText(translatedText_cursorState);
    if (ui->hideCursorComboBox->currentText() != tr("Idle")) {
        ui->idleTimeoutGroupBox->hide();
    }

    ui->idleTimeoutSpinBox->setValue(m_emu_settings->GetCursorHideTimeout());
    ui->usbComboBox->setCurrentIndex(m_emu_settings->GetUsbDeviceBackend());
    ui->micComboBox->setCurrentText(QString::fromStdString(m_emu_settings->GetMicDevice()));
    ui->motionControlsCheckBox->setChecked(m_emu_settings->IsMotionControlsEnabled());
    ui->backgroundControllerCheckBox->setChecked(m_emu_settings->IsBackgroundControllerInput());

    // ------------------ Log tab --------------------------------------------------------
    ui->logFilterLineEdit->setText(QString::fromStdString(m_emu_settings->GetLogFilter()));
    ui->enableLoggingCheckBox->setChecked(m_emu_settings->IsLogEnabled());
    ui->separateLogFilesCheckbox->setChecked(m_emu_settings->IsSeparateLoggingEnabled());
    ui->identicalLogGroupedCheckbox->setChecked(m_emu_settings->IsIdenticalLogGrouped());

    std::string logType = m_emu_settings->GetLogType();
    QString translatedText_LogType = logTypeMap.key(QString::fromStdString(logType));
    ui->logTypeComboBox->setCurrentText(translatedText_LogType);

    // ------------------ Debug tab --------------------------------------------------------
    ui->rdocCheckBox->setChecked(m_emu_settings->IsRenderdocEnabled());
    ui->dumpShadersCheckBox->setChecked(m_emu_settings->IsDumpShaders());
    ui->debugDump->setChecked(m_emu_settings->IsDebugDump());
    ui->copyGPUBuffersCheckBox->setChecked(m_emu_settings->IsCopyGpuBuffers());

    ui->vkValidationCheckBox->setChecked(m_emu_settings->IsVkValidationEnabled());
    ui->vkCoreValidationCheckBox->setChecked(m_emu_settings->IsVkValidationCoreEnabled());
    ui->vkSyncValidationCheckBox->setChecked(m_emu_settings->IsVkValidationSyncEnabled());
    ui->vkGpuValidationCheckBox->setChecked(m_emu_settings->IsVkValidationGpuEnabled());
    ui->vkValidationCheckBox->isChecked() ? ui->vkLayersGroupBox->setVisible(true)
                                          : ui->vkLayersGroupBox->setVisible(false);

    ui->collectShaderCheckBox->setChecked(m_emu_settings->IsShaderCollect());
    ui->crashDiagnosticsCheckBox->setChecked(m_emu_settings->IsVkCrashDiagnosticEnabled());
    ui->hostMarkersCheckBox->setChecked(m_emu_settings->IsVkHostMarkersEnabled());
    ui->guestMarkersCheckBox->setChecked(m_emu_settings->IsVkGuestMarkersEnabled());

    // ------------------ Experimental tab --------------------------------------------------------
    ui->readbacksModeComboBox->setCurrentIndex(m_emu_settings->GetReadbacksMode());
    ui->readbackLinearImagesCheckBox->setChecked(m_emu_settings->IsReadbackLinearImagesEnabled());
    ui->dmaCheckBox->setChecked(m_emu_settings->IsDirectMemoryAccessEnabled());
    ui->devkitCheckBox->setChecked(m_emu_settings->IsDevKit());
    ui->neoCheckBox->setChecked(m_emu_settings->IsNeo());
    ui->psnSignInCheckBox->setChecked(m_emu_settings->IsPSNSignedIn());
    ui->networkConnectedCheckBox->setChecked(m_emu_settings->IsConnectedToNetwork());

    ui->enableShaderCacheCheckBox->setChecked(m_emu_settings->IsPipelineCacheEnabled());
    ui->archiveShaderCacheCheckBox->setChecked(m_emu_settings->IsPipelineCacheArchived());
    ui->dmemSpinBox->setValue(m_emu_settings->GetExtraDmemInMBytes());
    ui->vblankSpinBox->setValue(m_emu_settings->GetVblankFrequency());

    // ------------------ Games Folder --------------------------------------------------------
    ui->gameFoldersListWidget->clear();
    const auto& dirs = m_emu_settings->GetAllGameInstallDirs();
    for (const auto& entry : dirs) {
        QString qpath;
        // Use existing helper to convert path -> QString; fallback if not available:
        Common::FS::PathToQString(qpath, entry.path);

        auto* item = new QListWidgetItem(qpath);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));

        if (!std::filesystem::exists(entry.path)) {
            item->setForeground(Qt::red);
            item->setToolTip(tr("This path does not exist on disk."));
        } else {
            item->setToolTip(QString());
        }

        ui->gameFoldersListWidget->addItem(item);
    }
    // ------------------ Addon Folder --------------------------------------------------------
    const auto dlc_folder_path = m_emu_settings->GetAddonInstallDir();
    QString dlc_folder_path_string;
    Common::FS::PathToQString(dlc_folder_path_string, dlc_folder_path);
    ui->currentDLCFolder->setText(dlc_folder_path_string);
    // ------------------ Home Folder --------------------------------------------------------
    const auto home_path = m_emu_settings->GetHomeDir();
    QString home_path_string;
    Common::FS::PathToQString(home_path_string, home_path);
    ui->currentHomePath->setText(home_path_string);
    // ------------------ Sys Modules Folder--------------------------------------------------
    const auto sysmodules_path = m_emu_settings->GetSysModulesDir();
    QString sysmodules_path_string;
    Common::FS::PathToQString(sysmodules_path_string, sysmodules_path);
    ui->currentSysmodulesPath->setText(sysmodules_path_string);
    // ------------------ Fonts Folder--------------------------------------------------
    const auto fonts_path = m_emu_settings->GetFontsDir();
    QString fonts_path_string;
    Common::FS::PathToQString(fonts_path_string, fonts_path);
    ui->currentFontsPath->setText(fonts_path_string);
    // ----------GUI Settings --------------------------------------
    ui->ScanDepthComboBox->setCurrentIndex(
        m_gui_settings->GetValue(GUI::general_directory_depth_scanning).toInt() - 1);
}

// ---------------------------- Compare backend vs UI ----------------------------
bool SettingsDialog::IsGameFoldersChanged() const {
    // Compare game install dirs
    auto backend = m_emu_settings->GetAllGameInstallDirs();
    std::vector<GameInstallDir> ui_dirs;

    ui_dirs.reserve(ui->gameFoldersListWidget->count());
    for (int i = 0; i < ui->gameFoldersListWidget->count(); ++i) {
        auto* item = ui->gameFoldersListWidget->item(i);
        GameInstallDir d;
        d.path = Common::FS::PathFromQString(item->text());
        d.enabled = (item->checkState() == Qt::Checked);
        ui_dirs.push_back(d);
    }

    if (backend != ui_dirs)
        return true;

    // Compare scan depth
    int backend_depth = m_gui_settings->GetValue(GUI::general_directory_depth_scanning).toInt();
    int ui_depth = ui->ScanDepthComboBox->currentIndex() + 1;
    if (backend_depth != ui_depth)
        return true;

    return false;
}

void SettingsDialog::ApplyValuesToBackend() {
    std::vector<GameInstallDir> dirs;
    dirs.reserve(ui->gameFoldersListWidget->count());

    // ------------------ General tab --------------------------------------------------------
    m_emu_settings->SetShowSplash(ui->showSplashCheckBox->isChecked());
    m_emu_settings->SetVolumeSlider(ui->horizontalVolumeSlider->value());
    m_emu_settings->SetMainOutputDevice(ui->GenAudioComboBox->currentText().toStdString());
    m_emu_settings->SetPadSpkOutputDevice(ui->DsAudioComboBox->currentText().toStdString());
    m_emu_settings->SetTrophyPopupDisabled(ui->disableTrophycheckBox->isChecked());
    m_emu_settings->SetTrophyNotificationDuration(ui->popUpDurationSpinBox->value());

    std::string trophy_loc;
    if (ui->radioButton_Top->isChecked()) {
        trophy_loc = "top";
    } else if (ui->radioButton_Left->isChecked()) {
        trophy_loc = "left";
    } else if (ui->radioButton_Right->isChecked()) {
        trophy_loc = "right";
    } else if (ui->radioButton_Bottom->isChecked()) {
        trophy_loc = "bottom";
    }
    m_emu_settings->SetTrophyNotificationSide(trophy_loc);
    m_emu_settings->SetShowFpsCounter(ui->showFpsCounterCheckBox->isChecked());

    // ------------------ GUI tab --------------------------------------------------------
    m_emu_settings->SetDiscordRPCEnabled(ui->discordRPCCheckbox->isChecked());

    m_gui_settings->SetValue(GUI::general_directory_depth_scanning,
                             ui->ScanDepthComboBox->currentIndex() + 1);
    m_gui_settings->SetValue(GUI::game_list_play_bg, ui->playBGMCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::game_list_bg_volume, ui->BGMVolumeSlider->value());
    m_gui_settings->SetValue(GUI::game_list_showBackgroundImage,
                             ui->showBackgroundImageCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::game_list_backgroundImageOpacity,
                             ui->backgroundImageOpacitySlider->value());
    m_gui_settings->SetValue(GUI::compatibility_check_on_startup,
                             ui->checkCompatibilityOnStartupCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::general_show_changelog, ui->changelogCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::general_check_gui_updates, ui->updaterCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::general_separate_update_folder,
                             ui->separateUpdateCheckBox->isChecked());

    // ------------------ Graphics tab --------------------------------------------------------
    bool isFullscreen = ui->displayModeComboBox->currentText() != tr("Windowed");
    m_emu_settings->SetFullScreen(isFullscreen);
    m_emu_settings->SetPresentMode(
        presentModeMap.value(ui->presentModeComboBox->currentText()).toStdString());
    m_emu_settings->SetFullScreenMode(
        screenModeMap.value(ui->displayModeComboBox->currentText()).toStdString());

    m_emu_settings->SetWindowHeight(ui->heightSpinBox->value());
    m_emu_settings->SetWindowWidth(ui->widthSpinBox->value());
    m_emu_settings->SetHdrAllowed(ui->enableHDRCheckBox->isChecked());

    m_emu_settings->SetFsrEnabled(ui->FSRCheckBox->isChecked());
    m_emu_settings->SetRcasEnabled(ui->RCASCheckBox->isChecked());
    m_emu_settings->SetRcasAttenuation(ui->RCASSlider->value());

    // First options is auto selection -1, so gpuId on the GUI will always have to subtract 1
    // when setting and add 1 when getting to select the correct gpu in Qt
    m_emu_settings->SetGpuId(ui->graphicsAdapterBox->currentIndex() - 1);

    // ------------------ Input tab --------------------------------------------------------
    m_emu_settings->SetCursorState(cursorStateMap.value(ui->hideCursorComboBox->currentText()));
    m_emu_settings->SetCursorHideTimeout(ui->idleTimeoutSpinBox->value());
    m_emu_settings->SetMicDevice(ui->micComboBox->currentText().toStdString());
    m_emu_settings->SetUsbDeviceBackend(ui->usbComboBox->currentIndex());
    m_emu_settings->SetMotionControlsEnabled(ui->motionControlsCheckBox->isChecked());
    m_emu_settings->SetBackgroundControllerInput(ui->backgroundControllerCheckBox->isChecked());

    // ------------------ Log tab --------------------------------------------------------
    m_emu_settings->SetLogFilter(ui->logFilterLineEdit->text().toStdString());
    m_emu_settings->SetLogEnabled(ui->enableLoggingCheckBox->isChecked());
    m_emu_settings->SetSeparateLoggingEnabled(ui->separateLogFilesCheckbox->isChecked());
    m_emu_settings->SetIdenticalLogGrouped(ui->identicalLogGroupedCheckbox->isChecked());
    m_emu_settings->SetLogType(logTypeMap.value(ui->logTypeComboBox->currentText()).toStdString());

    // ------------------ Debug tab --------------------------------------------------------
    m_emu_settings->SetRenderdocEnabled(ui->rdocCheckBox->isChecked());
    m_emu_settings->SetDumpShaders(ui->dumpShadersCheckBox->isChecked());
    m_emu_settings->SetDebugDump(ui->debugDump->isChecked());
    m_emu_settings->SetCopyGpuBuffers(ui->copyGPUBuffersCheckBox->isChecked());

    m_emu_settings->SetVkValidationEnabled(ui->vkValidationCheckBox->isChecked());
    m_emu_settings->SetVkValidationCoreEnabled(ui->vkCoreValidationCheckBox->isChecked());
    m_emu_settings->SetVkValidationSyncEnabled(ui->vkSyncValidationCheckBox->isChecked());
    m_emu_settings->SetVkValidationGpuEnabled(ui->vkGpuValidationCheckBox->isChecked());

    m_emu_settings->SetShaderCollect(ui->collectShaderCheckBox->isChecked());
    m_emu_settings->SetVkCrashDiagnosticEnabled(ui->crashDiagnosticsCheckBox->isChecked());
    m_emu_settings->SetVkHostMarkersEnabled(ui->hostMarkersCheckBox->isChecked());
    m_emu_settings->SetVkGuestMarkersEnabled(ui->guestMarkersCheckBox->isChecked());

    // ------------------ Experimental tab --------------------------------------------------------
    m_emu_settings->SetReadbacksMode(ui->readbacksModeComboBox->currentIndex());
    m_emu_settings->SetReadbackLinearImagesEnabled(ui->readbackLinearImagesCheckBox->isChecked());
    m_emu_settings->SetDirectMemoryAccessEnabled(ui->dmaCheckBox->isChecked());
    m_emu_settings->SetDevKit(ui->devkitCheckBox->isChecked());
    m_emu_settings->SetNeo(ui->neoCheckBox->isChecked());
    m_emu_settings->SetPSNSignedIn(ui->psnSignInCheckBox->isChecked());
    m_emu_settings->SetConnectedToNetwork(ui->networkConnectedCheckBox->isChecked());

    m_emu_settings->SetPipelineCacheEnabled(ui->enableShaderCacheCheckBox->isChecked());
    m_emu_settings->SetPipelineCacheArchived(ui->archiveShaderCacheCheckBox->isChecked());
    m_emu_settings->SetExtraDmemInMBytes(ui->dmemSpinBox->value());
    m_emu_settings->SetVblankFrequency(ui->vblankSpinBox->value());

    // ------------------ Paths tab --------------------------------------------------------
    for (int i = 0; i < ui->gameFoldersListWidget->count(); ++i) {
        auto* item = ui->gameFoldersListWidget->item(i);
        GameInstallDir d;
        d.path = Common::FS::PathFromQString(item->text());
        d.enabled = (item->checkState() == Qt::Checked);
        dirs.push_back(std::move(d));
    }
    m_emu_settings->SetAllGameInstallDirs(dirs);
    m_emu_settings->SetAddonInstallDir(Common::FS::PathFromQString(ui->currentDLCFolder->text()));
    m_emu_settings->SetHomeDir(Common::FS::PathFromQString(ui->currentHomePath->text()));
    m_emu_settings->SetSysModulesDir(
        Common::FS::PathFromQString(ui->currentSysmodulesPath->text()));
    m_emu_settings->SetFontsDir(Common::FS::PathFromQString(ui->currentFontsPath->text()));
}

// ---------------------------- Button box handling ----------------------------
void SettingsDialog::HandleButtonBox() {
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        auto* applyBtn = ui->buttonBox->button(QDialogButtonBox::Apply);
        auto* saveBtn = ui->buttonBox->button(QDialogButtonBox::Save);
        auto* restoreBtn = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
        auto* closeBtn = ui->buttonBox->button(QDialogButtonBox::Close);

        // APPLY: update backend (memory) and emit only if changed
        if (button == applyBtn) {
            bool changed = IsGameFoldersChanged();
            ApplyValuesToBackend();

            if (changed) {
                emit GameFoldersChanged();
            }

            if (!IsGlobal()) {
                // Save game-specific overrides immediately
                if (!m_emu_settings->Save(m_game_serial)) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to save game settings."));
                } else {
                    QMessageBox::information(this, tr("Settings Applied"),
                                             tr("Game-specific settings have been saved."));
                }
            }
            return;
        }

        // SAVE: apply, emit if changed, then persist and close
        if (button == saveBtn) {
            bool changed = IsGameFoldersChanged();
            ApplyValuesToBackend();

            if (changed) {
                emit GameFoldersChanged();
            }

            if (IsGlobal()) {
                // Save global settings
                if (!m_emu_settings->Save()) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to save global settings."));
                    return;
                }
            } else {
                // Save game-specific overrides
                if (!m_emu_settings->Save(m_game_serial)) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to save game settings."));
                    return;
                }
            }

            close();
            Q_EMIT EmuSettingsApplied();
            return;
        }

        if (button == restoreBtn) {
            QString message =
                IsGlobal()
                    ? tr("Are you sure you want to restore all settings to their default values?")
                    : tr("Are you sure you want to restore all settings to global defaults?\n"
                         "This will remove all game-specific overrides.");

            const auto reply = QMessageBox::question(this, tr("Restore Defaults"), message,
                                                     QMessageBox::Yes | QMessageBox::No);

            if (reply != QMessageBox::Yes)
                return;

            if (IsGlobal()) {
                // Snapshot before defaults
                const auto before = m_emu_settings->GetAllGameInstallDirs();
                m_emu_settings->SetDefaultValues();
                const auto after = m_emu_settings->GetAllGameInstallDirs();

                // Update UI to reflect defaults
                LoadValuesFromConfig();

                if (before != after) {
                    emit GameFoldersChanged();
                }
            } else {
                // For game-specific: restore to global settings
                if (m_original_settings) {
                    // Restore from backup
                    *m_emu_settings = *m_original_settings;

                    // Delete game-specific config file
                    if (!m_game_serial.empty()) {
                        const auto gamePath =
                            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                            (m_game_serial + ".json");
                        std::filesystem::remove(gamePath);
                    }

                    // Update UI
                    LoadValuesFromConfig();

                    QMessageBox::information(this, tr("Settings Restored"),
                                             tr("Game settings restored to global defaults."));
                }
            }
            Q_EMIT EmuSettingsApplied();
            return;
        }

        // CLOSE
        if (button == closeBtn) {
            close();
            Q_EMIT EmuSettingsApplied();
            return;
        }
    });

    ui->buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save"));
    ui->buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    ui->buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));

    connect(ui->tabWidgetSettings, &QTabWidget::currentChanged, this,
            [this]() { ui->buttonBox->button(QDialogButtonBox::Close)->setFocus(); });
}

void SettingsDialog::PopulateComboBoxes() {
    // GPU Devices
    ui->graphicsAdapterBox->addItem(tr("Auto Select")); // -1, auto selection
    const int maxDevices = GetVulkanDeviceCount();
    const int maxNameLength = 100;
    std::vector<char*> names(maxDevices);
    for (int i = 0; i < maxDevices; ++i)
        names[i] = new char[maxNameLength];
    GetVulkanDeviceNames(names.data(), maxDevices, maxNameLength);
    for (int i = 0; i < maxDevices; ++i) {
        ui->graphicsAdapterBox->addItem(names[i]);
        delete[] names[i];
    }

    // Audio Devices
    SDL_InitSubSystem(SDL_INIT_AUDIO);

    ui->GenAudioComboBox->addItem(tr("Default Device"), "Default Device");
    ui->DsAudioComboBox->addItem(tr("Default Device"), "Default Device");
    int playback_count = 0;
    SDL_AudioDeviceID* pdevices = SDL_GetAudioPlaybackDevices(&playback_count);
    if (pdevices) {
        for (int i = 0; i < playback_count; ++i) {
            SDL_AudioDeviceID devId = pdevices[i];
            const char* name = SDL_GetAudioDeviceName(devId);
            if (name) {
                QString qname = QString::fromUtf8(name);
                ui->GenAudioComboBox->addItem(qname, QString::number(devId));
                ui->DsAudioComboBox->addItem(qname, QString::number(devId));
            }
        }
        SDL_free(pdevices);
    } else {
        qDebug() << "Error getting audio devices: " << SDL_GetError();
    }

    ui->micComboBox->addItem(micMap.key("None"), "None");
    ui->micComboBox->addItem(micMap.key("Default Device"), "Default Device");
    int recording_count = 0;
    SDL_AudioDeviceID* rdevices = SDL_GetAudioRecordingDevices(&recording_count);
    if (rdevices) {
        for (int i = 0; i < recording_count; ++i) {
            SDL_AudioDeviceID devId = rdevices[i];
            const char* name = SDL_GetAudioDeviceName(devId);
            if (name) {
                QString qname = QString::fromUtf8(name);
                ui->micComboBox->addItem(qname, QString::number(devId));
            }
        }
        SDL_free(rdevices);
    } else {
        qDebug() << "Erro SDL_GetAudioRecordingDevices:" << SDL_GetError();
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_Quit();

    // Other Settings
    ui->hideCursorComboBox->addItem(tr("Never"));
    ui->hideCursorComboBox->addItem(tr("Idle"));
    ui->hideCursorComboBox->addItem(tr("Always"));

    ui->usbComboBox->addItem(tr("Real USB Device"));
    ui->usbComboBox->addItem(tr("Skylander Portal"));
    ui->usbComboBox->addItem(tr("Infinity Base"));
    ui->usbComboBox->addItem(tr("Dimensions Toypad"));
}

bool SettingsDialog::IsSettingOverrideable(const char* setting_key,
                                           const QString& setting_group) const {
    // Check if the setting is in the overrideable list for the given group
    if (setting_group == "General") {
        for (const auto& item : m_emu_settings->GetGeneralOverrideableFields()) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
    } else if (setting_group == "Debug") {
        for (const auto& item : m_emu_settings->GetDebugOverrideableFields()) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
    } else if (setting_group == "Input") {
        for (const auto& item : m_emu_settings->GetInputOverrideableFields()) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
    } else if (setting_group == "Audio") {
        for (const auto& item : m_emu_settings->GetAudioOverrideableFields()) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
    } else if (setting_group == "GPU") {
        for (const auto& item : m_emu_settings->GetGPUOverrideableFields()) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
    } else if (setting_group == "Vulkan") {
        for (const auto& item : m_emu_settings->GetVulkanOverrideableFields()) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
    }

    return false;
}

void SettingsDialog::MapUIControls() {
    // General Settings
    m_uiSettingMap[ui->showSplashCheckBox] = {"show_splash", "General"};
    m_uiSettingMap[ui->horizontalVolumeSlider] = {"volume_slider", "General"};
    m_uiSettingMap[ui->disableTrophycheckBox] = {"trophy_popup_disabled", "General"};
    m_uiSettingMap[ui->popUpDurationSpinBox] = {"trophy_notification_duration", "General"};
    m_uiSettingMap[ui->radioButton_Top] = {"trophy_notification_side", "General"};
    m_uiSettingMap[ui->radioButton_Left] = {"trophy_notification_side", "General"};
    m_uiSettingMap[ui->radioButton_Right] = {"trophy_notification_side", "General"};
    m_uiSettingMap[ui->radioButton_Bottom] = {"trophy_notification_side", "General"};
    m_uiSettingMap[ui->showFpsCounterCheckBox] = {"show_fps_counter", "General"};
    m_uiSettingMap[ui->discordRPCCheckbox] = {"discord_rpc_enabled", "General"};

    // Audio Settings
    m_uiSettingMap[ui->GenAudioComboBox] = {"main_output_device", "Audio"};
    m_uiSettingMap[ui->DsAudioComboBox] = {"padSpk_output_device", "Audio"};
    m_uiSettingMap[ui->micComboBox] = {"mic_device", "Audio"};

    // GPU Settings
    m_uiSettingMap[ui->graphicsAdapterBox] = {"gpu_id", "Vulkan"}; // Note: This is in Vulkan group
    m_uiSettingMap[ui->nullGpuCheckBox] = {"null_gpu", "GPU"};
    m_uiSettingMap[ui->heightSpinBox] = {"window_height", "GPU"};
    m_uiSettingMap[ui->widthSpinBox] = {"window_width", "GPU"};
    m_uiSettingMap[ui->enableHDRCheckBox] = {"hdr_allowed", "GPU"};
    m_uiSettingMap[ui->FSRCheckBox] = {"fsr_enabled", "GPU"};
    m_uiSettingMap[ui->RCASCheckBox] = {"rcas_enabled", "GPU"};
    m_uiSettingMap[ui->RCASSlider] = {"rcas_attenuation", "GPU"};
    m_uiSettingMap[ui->dumpShadersCheckBox] = {"dump_shaders", "GPU"};
    m_uiSettingMap[ui->copyGPUBuffersCheckBox] = {"copy_gpu_buffers", "GPU"};
    m_uiSettingMap[ui->displayModeComboBox] = {"full_screen_mode", "GPU"};
    m_uiSettingMap[ui->presentModeComboBox] = {"present_mode", "GPU"};
    m_uiSettingMap[ui->readbacksModeComboBox] = {"readbacks_mode", "GPU"};
    m_uiSettingMap[ui->readbackLinearImagesCheckBox] = {"readback_linear_images_enabled", "GPU"};
    m_uiSettingMap[ui->dmaCheckBox] = {"direct_memory_access_enabled", "GPU"};

    // Input Settings
    m_uiSettingMap[ui->hideCursorComboBox] = {"cursor_state", "Input"};
    m_uiSettingMap[ui->idleTimeoutSpinBox] = {"cursor_hide_timeout", "Input"};
    m_uiSettingMap[ui->usbComboBox] = {"usb_device_backend", "Input"};
    m_uiSettingMap[ui->motionControlsCheckBox] = {"motion_controls_enabled", "Input"};
    m_uiSettingMap[ui->backgroundControllerCheckBox] = {"background_controller_input", "Input"};

    // Debug Settings
    m_uiSettingMap[ui->separateLogFilesCheckbox] = {"separate_logging_enabled", "Debug"};
    m_uiSettingMap[ui->identicalLogGroupedCheckbox] = {"identical_log_grouped", "General"};
    m_uiSettingMap[ui->debugDump] = {"debug_dump", "Debug"};
    m_uiSettingMap[ui->collectShaderCheckBox] = {"shader_collect", "Debug"};
    m_uiSettingMap[ui->enableLoggingCheckBox] = {"log_enabled", "Debug"};
    m_uiSettingMap[ui->logFilterLineEdit] = {"log_filter", "General"};
    m_uiSettingMap[ui->logTypeComboBox] = {"log_type", "General"};

    // Vulkan Settings
    m_uiSettingMap[ui->rdocCheckBox] = {"renderdoc_enabled", "Vulkan"};
    m_uiSettingMap[ui->vkValidationCheckBox] = {"vkvalidation_enabled", "Vulkan"};
    m_uiSettingMap[ui->vkCoreValidationCheckBox] = {"vkvalidation_core_enabled", "Vulkan"};
    m_uiSettingMap[ui->vkSyncValidationCheckBox] = {"vkvalidation_sync_enabled", "Vulkan"};
    m_uiSettingMap[ui->vkGpuValidationCheckBox] = {"vkvalidation_gpu_enabled", "Vulkan"};
    m_uiSettingMap[ui->crashDiagnosticsCheckBox] = {"vkcrash_diagnostic_enabled", "Vulkan"};
    m_uiSettingMap[ui->hostMarkersCheckBox] = {"vkhost_markers", "Vulkan"};
    m_uiSettingMap[ui->guestMarkersCheckBox] = {"vkguest_markers", "Vulkan"};
    m_uiSettingMap[ui->enableShaderCacheCheckBox] = {"pipeline_cache_enabled", "Vulkan"};
    m_uiSettingMap[ui->archiveShaderCacheCheckBox] = {"pipeline_cache_archived", "Vulkan"};

    // Experimental/Other Settings
    m_uiSettingMap[ui->devkitCheckBox] = {"dev_kit_mode", "General"};
    m_uiSettingMap[ui->neoCheckBox] = {"neo_mode", "General"};
    m_uiSettingMap[ui->psnSignInCheckBox] = {"psn_signed_in", "General"};
    m_uiSettingMap[ui->networkConnectedCheckBox] = {"connected_to_network", "General"};
    m_uiSettingMap[ui->dmemSpinBox] = {"extra_dmem_in_mbytes", "General"};
    m_uiSettingMap[ui->vblankSpinBox] = {"vblank_frequency", "GPU"};
}

void SettingsDialog::DisableNonOverrideableSettings() {
    if (m_game_serial.empty()) {
        // Global settings dialog - don't disable anything
        return;
    }

    // For game-specific settings dialog, disable non-overrideable controls
    for (auto it = m_uiSettingMap.begin(); it != m_uiSettingMap.end(); ++it) {
        QObject* control = it.key();
        const char* setting_key = it.value().first;
        const QString& setting_group = it.value().second;

        if (!IsSettingOverrideable(setting_key, setting_group)) {
            QWidget* widget = qobject_cast<QWidget*>(control);
            if (widget) {
                widget->setEnabled(false);
                widget->setToolTip(tr("This setting cannot be overridden per-game. "
                                      "Use global settings to change it."));

                // For checkboxes and comboboxes, also set visual cue
                QCheckBox* checkbox = qobject_cast<QCheckBox*>(control);
                if (checkbox) {
                    checkbox->setStyleSheet("QCheckBox:disabled { color: gray; }");
                }

                QComboBox* combo = qobject_cast<QComboBox*>(control);
                if (combo) {
                    combo->setStyleSheet("QComboBox:disabled { color: gray; }");
                }

                QSpinBox* spin = qobject_cast<QSpinBox*>(control);
                if (spin) {
                    spin->setStyleSheet("QSpinBox:disabled { color: gray; }");
                }

                QSlider* slider = qobject_cast<QSlider*>(control);
                if (slider) {
                    slider->setStyleSheet("QSlider:disabled { color: gray; }");
                }
            }
        }
    }

    // Special handling for controls not directly mapped
    // GUI-only controls (not in emulator settings)
    QList<QObject*> guiOnlyControls = {ui->playBGMCheckBox,
                                       ui->BGMVolumeSlider,
                                       ui->showBackgroundImageCheckBox,
                                       ui->backgroundImageOpacitySlider,
                                       ui->checkCompatibilityOnStartupCheckBox,
                                       ui->updaterCheckBox,
                                       ui->changelogCheckBox,
                                       ui->separateUpdateCheckBox,
                                       ui->ScanDepthComboBox};

    for (QObject* control : guiOnlyControls) {
        QWidget* widget = qobject_cast<QWidget*>(control);
        if (widget) {
            widget->setEnabled(false);
            widget->setToolTip(tr("GUI-only settings cannot be overridden per-game. "
                                  "Use global settings to change them."));
        }
    }

    // Path controls (always global)
    QList<QObject*> pathControls = {
        ui->gameFoldersListWidget, ui->addFolderButton,       ui->removeFolderButton,
        ui->currentDLCFolder,      ui->browseDLCButton,       ui->currentHomePath,
        ui->browseHomeButton,      ui->currentSysmodulesPath, ui->browseSysmodulesButton,
        ui->gameFoldersGroupBox,   ui->dlcFolderGroupBox,     ui->homeGroupBox,
        ui->sysmodulesGroupBox};

    for (QObject* control : pathControls) {
        QWidget* widget = qobject_cast<QWidget*>(control);
        if (widget) {
            widget->setEnabled(false);
            widget->setToolTip(tr("Path settings cannot be overridden per-game. "
                                  "Use global settings to change them."));
        }
    }
}
