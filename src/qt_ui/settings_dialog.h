// SPDX-FileCopyrightText: Copyright 2025 shadLauncher 4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QLabel>
#include <QSlider>

#include <memory>

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
                            std::shared_ptr<EmulatorSettings> emu_settings, int tab_index = 0,
                            QWidget* parent = nullptr, const GameInfo* game = nullptr,
                            bool global = true);
    ~SettingsDialog();
    void open() override;

signals:
    void GameFoldersChanged();

private:
    int m_tab_index = 0;
    std::unique_ptr<Ui::SettingsDialog> ui;
    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettings> m_emu_settings;

    // help texts
    QString m_description;
    QHash<QObject*, QString> m_descriptions;

    void SubscribeHelpText(QObject* object, const QString& text);
    bool eventFilter(QObject* object, QEvent* event) override;
    void PathTabConnections();
    void LoadValuesFromConfig();
    bool IsGameFoldersChanged() const;
    void HandleButtonBox();
    void ApplyValuesToBackend();
};