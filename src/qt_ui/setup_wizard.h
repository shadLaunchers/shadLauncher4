// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include <QWizard>
#include <QWizardPage>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class EmulatorSettingsImpl;
class GUISettings;
class SetupWizard;

class SetupWizard final : public QWizard {
    Q_OBJECT

public:
    enum PageId {
        Page_Intro = 0,
        Page_Folders,
        Page_Version,
        Page_Conclusion,
    };

    explicit SetupWizard(std::shared_ptr<GUISettings> gui_settings,
                         std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                         QWidget* parent = nullptr);
    ~SetupWizard() override;

    // True once the user has finished the wizard at least once.
    static bool IsSetupCompleted(const std::shared_ptr<GUISettings>& gui_settings);

    // Pages call these instead of emitting the wizard's signals directly.
    void RequestLanguageChange(const QString& language_code);
    void RequestThemeChange();

    void accept() override;

Q_SIGNALS:
    void requestLanguageChange(const QString& language_code);
    void requestThemeChange();

protected:
    void changeEvent(QEvent* event) override;

private:
    void Retranslate();

    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings;
};

// Page 1 - welcome and language
class SetupWizardIntroPage final : public QWizardPage {
    Q_OBJECT

public:
    explicit SetupWizardIntroPage(SetupWizard* wizard, std::shared_ptr<GUISettings> gui_settings,
                                  QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private:
    void Retranslate();
    void PopulateLanguages();
    void PopulateThemes();

    SetupWizard* m_wizard = nullptr;
    std::shared_ptr<GUISettings> m_gui_settings;

    QLabel* m_intro_label = nullptr;
    QLabel* m_language_label = nullptr;
    QComboBox* m_language_combo = nullptr;
    QLabel* m_theme_label = nullptr;
    QComboBox* m_theme_combo = nullptr;
};

// Page 2 - games / DLC / emulator version folders
class SetupWizardFoldersPage final : public QWizardPage {
    Q_OBJECT

public:
    explicit SetupWizardFoldersPage(std::shared_ptr<GUISettings> gui_settings,
                                    std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                                    QWidget* parent = nullptr);

    void initializePage() override;
    bool validatePage() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void Retranslate();
    void BrowseInto(QLineEdit* target, const QString& caption);

    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings;

    QLabel* m_games_label = nullptr;
    QLabel* m_dlc_label = nullptr;
    QLabel* m_versions_label = nullptr;
    QLabel* m_hint_label = nullptr;
    QLineEdit* m_games_edit = nullptr;
    QLineEdit* m_dlc_edit = nullptr;
    QLineEdit* m_versions_edit = nullptr;
    QPushButton* m_games_browse = nullptr;
    QPushButton* m_dlc_browse = nullptr;
    QPushButton* m_versions_browse = nullptr;
};

// Page 3 - emulator version
class SetupWizardVersionPage final : public QWizardPage {
    Q_OBJECT

public:
    explicit SetupWizardVersionPage(std::shared_ptr<GUISettings> gui_settings,
                                    QWidget* parent = nullptr);

    void initializePage() override;
    bool validatePage() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void Retranslate();
    void RefreshVersionList();
    void OpenVersionManager();

    std::shared_ptr<GUISettings> m_gui_settings;

    QLabel* m_info_label = nullptr;
    QLabel* m_empty_label = nullptr;
    QListWidget* m_version_list = nullptr;
    QPushButton* m_manage_button = nullptr;
};

// Page 4 - summary
class SetupWizardConclusionPage final : public QWizardPage {
    Q_OBJECT

public:
    explicit SetupWizardConclusionPage(std::shared_ptr<GUISettings> gui_settings,
                                       std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                                       QWidget* parent = nullptr);

    void initializePage() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void Retranslate();

    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings;

    QLabel* m_done_label = nullptr;
    QLabel* m_summary_label = nullptr;
};
