// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QApplication>
#include <QTranslator>
#include "common/types.h"

class EmulatorSettings;
class GUISettings;
class PersistentSettings;
class MainWindow;
class IpcClient;
class EmulatorState;

class GUIApplication : public QApplication {
    Q_OBJECT
public:
    GUIApplication(int& argc, char** argv);
    ~GUIApplication();

    bool init();
    static s32 getLanguageId();
    static QStringList getAvailableLanguageCodes();

    MainWindow* m_main_window = nullptr;
private Q_SLOTS:
    void OnChangeStyleSheetRequest();

private:
    void switchTranslator(QTranslator& translator, const QString& filename,
                          const QString& language_code);
    void loadLanguage(const QString& language_code);
    void setLanguageCode(QString language_code);
    void InitializeConnects();

    std::shared_ptr<EmulatorSettings> m_emu_settings;
    std::shared_ptr<EmulatorState> m_emu_state;
    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<PersistentSettings> m_persistent_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
    QString m_default_style;
    QTranslator m_translator;
    QString m_language_code;
    static s32 m_language_id;
};
