// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QLabel>
#include <QSlider>

#include <memory>
#include "core/emulator_settings.h"
#include "core/ipc/ipc_client.h"

class GUISettings;
class EmulatorSettings;
class GameInfo;

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(std::shared_ptr<GUISettings> gui_settings,
                            std::shared_ptr<EmulatorSettings> emu_settings,
                            std::shared_ptr<IpcClient> ipc_client, int tab_index = 0,
                            QWidget* parent = nullptr, const GameInfo* game = nullptr,
                            bool customFromGlobal = false);
    ~SettingsDialog();
    void open() override;

signals:
    void GameFoldersChanged();
    void CompatUpdateRequested();
    void EmuSettingsApplied();

private:
    int m_tab_index = 0;
    std::unique_ptr<Ui::SettingsDialog> ui;
    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
    bool m_custom_settings_from_global;
    GameInfo m_current_game;   // Add current game info
    std::string m_game_serial; // Game serial number

    bool IsGlobal() {
        return (!m_custom_settings_from_global);
    }

    std::shared_ptr<EmulatorSettings> m_game_specific_settings;
    std::shared_ptr<EmulatorSettings> m_emu_settings;
    std::shared_ptr<EmulatorSettings> m_original_settings;

    // help texts
    QString m_description;
    QHash<QObject*, QString> m_descriptions;

    void SubscribeHelpText(QObject* object, const QString& text);
    bool eventFilter(QObject* object, QEvent* event) override;
    void PathTabConnections();
    void OtherConnections();
    void LoadValuesFromConfig();
    bool IsGameFoldersChanged() const;
    void HandleButtonBox();
    void ApplyValuesToBackend();
    void PopulateComboBoxes();
    bool IsSettingOverrideable(const char* setting_key, const QString& setting_group) const;
    void DisableNonOverrideableSettings();
    void MapUIControls();

    const QMap<QString, HideCursorState> cursorStateMap = {{tr("Never"), HideCursorState::Never},
                                                           {tr("Idle"), HideCursorState::Idle},
                                                           {tr("Always"), HideCursorState::Always}};

    const QMap<QString, UsbBackendType> usbDeviceMap = {
        {tr("Real USB Device"), UsbBackendType::Real},
        {tr("Skylander Portal"), UsbBackendType::SkylandersPortal},
        {tr("Infinity Base"), UsbBackendType::InfinityBase},
        {tr("Dimensions Toypad"), UsbBackendType::DimensionsToypad}};

    const QMap<QString, QString> presentModeMap = {{tr("Mailbox (Vsync)"), "Mailbox"},
                                                   {tr("Fifo (Vsync)"), "Fifo"},
                                                   {tr("Immediate (No Vsync)"), "Immediate"}};

    const QMap<QString, QString> screenModeMap = {
        {tr("Fullscreen (Borderless)"), "Fullscreen (Borderless)"},
        {tr("Windowed"), "Windowed"},
        {tr("Fullscreen"), "Fullscreen"}};

    const QMap<QString, QString> micMap = {{tr("None"), "None"},
                                           {tr("Default Device"), "Default Device"}};

    const QMap<QString, QString> logTypeMap = {{tr("async"), "async"}, {tr("sync"), "sync"}};

    // Map UI controls to their setting keys
    QMap<QObject*, std::pair<const char*, QString>> m_uiSettingMap;
};
