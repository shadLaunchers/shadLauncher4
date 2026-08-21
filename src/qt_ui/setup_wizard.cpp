// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStyleFactory>
#include <QVBoxLayout>

#include "common/path_util.h"
#include "common/versions.h"
#include "core/emulator_settings.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "qt_utils.h"
#include "setup_wizard.h"
#include "version_dialog.h"

QString VersionTypeName(VersionManager::VersionType type) {
    switch (type) {
    case VersionManager::VersionType::Release:
        return QCoreApplication::translate("SetupWizardVersionPage", "Release");
    case VersionManager::VersionType::Nightly:
        return QCoreApplication::translate("SetupWizardVersionPage", "Pre-release");
    case VersionManager::VersionType::Custom:
        return QCoreApplication::translate("SetupWizardVersionPage", "Local");
    }
    return QCoreApplication::translate("SetupWizardVersionPage", "Unknown");
}

QString DefaultUserPathAsQString(Common::FS::PathType type) {
    QString out;
    Common::FS::PathToQString(out, GUI::Utils::NormalizePath(Common::FS::GetUserPath(type)));
    return out;
}

SetupWizard::SetupWizard(std::shared_ptr<GUISettings> gui_settings,
                         std::shared_ptr<EmulatorSettingsImpl> emu_settings, QWidget* parent)
    : QWizard(parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emu_settings)) {

    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::NoCancelButtonOnLastPage, true);
    setPixmap(QWizard::LogoPixmap,
              QPixmap(":/images/shadLauncher4.png")
                  .scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    setWindowIcon(QIcon(":/images/shadLauncher4.ico"));

    setPage(Page_Intro, new SetupWizardIntroPage(this, m_gui_settings));
    setPage(Page_Folders, new SetupWizardFoldersPage(m_gui_settings, m_emu_settings));
    setPage(Page_Version, new SetupWizardVersionPage(m_gui_settings));
    setPage(Page_Conclusion, new SetupWizardConclusionPage(m_gui_settings, m_emu_settings));

    setStartId(Page_Intro);
    resize(640, 480);

    Retranslate();
}

SetupWizard::~SetupWizard() = default;

bool SetupWizard::IsSetupCompleted(const std::shared_ptr<GUISettings>& gui_settings) {
    if (!gui_settings) {
        return false;
    }
    return gui_settings->GetValue(GUI::general_setup_completed).toBool();
}

void SetupWizard::RequestLanguageChange(const QString& language_code) {
    Q_EMIT requestLanguageChange(language_code);
}

void SetupWizard::RequestThemeChange() {
    Q_EMIT requestThemeChange();
}

void SetupWizard::accept() {
    // Only a completed run counts - quitting early leaves the flag alone so the
    // wizard comes back on the next start.
    m_gui_settings->SetValue(GUI::general_setup_completed, true);
    if (m_emu_settings) {
        m_emu_settings->Save();
    }
    QWizard::accept();
}

void SetupWizard::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
    }
    QWizard::changeEvent(event);
}

void SetupWizard::Retranslate() {
    setWindowTitle(tr("shadLauncher4 Setup"));
}

// Page 1 - welcome and language
SetupWizardIntroPage::SetupWizardIntroPage(SetupWizard* wizard,
                                           std::shared_ptr<GUISettings> gui_settings,
                                           QWidget* parent)
    : QWizardPage(parent), m_wizard(wizard), m_gui_settings(std::move(gui_settings)) {

    auto* layout = new QVBoxLayout(this);

    m_intro_label = new QLabel(this);
    m_intro_label->setWordWrap(true);
    layout->addWidget(m_intro_label);
    layout->addSpacing(12);

    auto* form = new QFormLayout();
    m_language_label = new QLabel(this);
    m_language_combo = new QComboBox(this);
    m_theme_label = new QLabel(this);
    m_theme_combo = new QComboBox(this);
    form->addRow(m_language_label, m_language_combo);
    form->addRow(m_theme_label, m_theme_combo);
    layout->addLayout(form);

    layout->addStretch();

    PopulateLanguages();

    connect(m_language_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString code = m_language_combo->itemData(index).toString();
        if (!code.isEmpty() && m_wizard) {
            m_wizard->RequestLanguageChange(code);
        }
    });

    // Themes apply immediately and are persisted right away, matching how the
    // theme picker in Settings behaves.
    connect(m_theme_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString theme = m_theme_combo->itemData(index).toString();
        if (theme.isEmpty() || !m_gui_settings) {
            return;
        }
        if (m_gui_settings->GetValue(GUI::meta_currentStylesheet).toString() == theme) {
            return;
        }
        m_gui_settings->SetValue(GUI::meta_currentStylesheet, theme);
        if (m_wizard) {
            m_wizard->RequestThemeChange();
        }
    });

    Retranslate();
}

void SetupWizardIntroPage::PopulateLanguages() {
    const QString default_code = QLocale(QLocale::English, QLocale::UnitedStates).name();
    QStringList codes = GUIApplication::getAvailableLanguageCodes();

    // Keep English (US) available and first
    codes.removeAll(default_code);
    codes.prepend(default_code);

    const QString current = m_gui_settings
                                ? m_gui_settings->GetValue(GUI::localization_language).toString()
                                : default_code;

    m_language_combo->clear();
    for (const QString& code : codes) {
        const QLocale locale(code);
        QString name = locale.nativeLanguageName();
        if (locale.territory() != QLocale::AnyTerritory) {
            name += " (" + locale.nativeTerritoryName() + ")";
        }
        m_language_combo->addItem(name, code);
    }

    const int index = m_language_combo->findData(current);
    m_language_combo->setCurrentIndex(index >= 0 ? index : 0);
}

void SetupWizardIntroPage::PopulateThemes() {
    const QString current = m_gui_settings
                                ? m_gui_settings->GetValue(GUI::meta_currentStylesheet).toString()
                                : GUI::DefaultStylesheet;

    m_theme_combo->clear();
    m_theme_combo->addItem(tr("Default"), GUI::DefaultStylesheet);
    m_theme_combo->addItem(tr("None"), GUI::NoStylesheet);
    for (const QString& style : QStyleFactory::keys()) {
        const QString display = GUI::NativeStylesheet + " (" + style + ")";
        m_theme_combo->addItem(display, display);
    }
    if (m_gui_settings) {
        for (const QString& entry : m_gui_settings->GetStylesheetEntries()) {
            m_theme_combo->addItem(entry, entry);
        }
    }

    int index = m_theme_combo->findData(current);
    if (index < 0) {
        m_theme_combo->addItem(current + tr(" (missing)"), current);
        index = m_theme_combo->count() - 1;
    }
    m_theme_combo->setCurrentIndex(index);
}

void SetupWizardIntroPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
    }
    QWizardPage::changeEvent(event);
}

void SetupWizardIntroPage::Retranslate() {
    setTitle(tr("Welcome to shadLauncher4"));
    setSubTitle(tr("This short setup gets your library and emulator ready."));

    m_intro_label->setText(tr("shadLauncher4 manages your PS4 game library and launches it with "
                              "shadPS4.\n\nThe next steps ask where your games live and which "
                              "emulator version to use. You can change any of this later in "
                              "Settings."));
    m_language_label->setText(tr("Language:"));
    m_theme_label->setText(tr("Theme:"));

    // Rebuilt here so the translated built-in entries follow the language change.
    PopulateThemes();
}

// Page 2 - folders
SetupWizardFoldersPage::SetupWizardFoldersPage(std::shared_ptr<GUISettings> gui_settings,
                                               std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                                               QWidget* parent)
    : QWizardPage(parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emu_settings)) {

    auto* layout = new QVBoxLayout(this);
    auto* grid = new QGridLayout();

    m_games_label = new QLabel(this);
    m_dlc_label = new QLabel(this);
    m_versions_label = new QLabel(this);

    m_games_edit = new QLineEdit(this);
    m_dlc_edit = new QLineEdit(this);
    m_versions_edit = new QLineEdit(this);

    m_games_browse = new QPushButton(this);
    m_dlc_browse = new QPushButton(this);
    m_versions_browse = new QPushButton(this);

    grid->addWidget(m_games_label, 0, 0);
    grid->addWidget(m_games_edit, 0, 1);
    grid->addWidget(m_games_browse, 0, 2);
    grid->addWidget(m_dlc_label, 1, 0);
    grid->addWidget(m_dlc_edit, 1, 1);
    grid->addWidget(m_dlc_browse, 1, 2);
    grid->addWidget(m_versions_label, 2, 0);
    grid->addWidget(m_versions_edit, 2, 1);
    grid->addWidget(m_versions_browse, 2, 2);
    grid->setColumnStretch(1, 1);

    layout->addLayout(grid);

    m_hint_label = new QLabel(this);
    m_hint_label->setWordWrap(true);
    layout->addWidget(m_hint_label);
    layout->addStretch();

    connect(m_games_browse, &QPushButton::clicked, this,
            [this] { BrowseInto(m_games_edit, tr("Directory with your dumped games")); });
    connect(m_dlc_browse, &QPushButton::clicked, this,
            [this] { BrowseInto(m_dlc_edit, tr("Directory with your dumped DLCs")); });
    connect(m_versions_browse, &QPushButton::clicked, this,
            [this] { BrowseInto(m_versions_edit, tr("Directory to install emulator versions")); });

    Retranslate();
}

void SetupWizardFoldersPage::BrowseInto(QLineEdit* target, const QString& caption) {
    const QString dir = QFileDialog::getExistingDirectory(this, caption, target->text());
    if (!dir.isEmpty()) {
        target->setText(dir);
    }
}

void SetupWizardFoldersPage::initializePage() {
    // Games: first configured library folder, if any.
    const auto install_dirs = m_emu_settings->GetGameInstallDirs();
    if (!install_dirs.empty()) {
        QString tmp;
        Common::FS::PathToQString(tmp, GUI::Utils::NormalizePath(install_dirs.front()));
        m_games_edit->setText(tmp);
    }

    // DLC: configured addon folder, else the default one under the user dir.
    if (!m_emu_settings->GetAddonInstallDir().empty()) {
        QString tmp;
        Common::FS::PathToQString(tmp,
                                  GUI::Utils::NormalizePath(m_emu_settings->GetAddonInstallDir()));
        m_dlc_edit->setText(tmp);
    } else {
        m_dlc_edit->setText(DefaultUserPathAsQString(Common::FS::PathType::AddonDir));
    }

    // Versions: configured path, else the default one under the user dir.
    const QString version_path =
        m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();
    if (!version_path.isEmpty()) {
        m_versions_edit->setText(version_path);
    } else {
        m_versions_edit->setText(DefaultUserPathAsQString(Common::FS::PathType::VersionDir));
    }
}

bool SetupWizardFoldersPage::validatePage() {
    const QString g = m_games_edit->text().trimmed();
    const QString a = m_dlc_edit->text().trimmed();
    const QString v = m_versions_edit->text().trimmed();

    // Games folder must already exist - we are not creating a library out of thin air.
    if (g.isEmpty() || !QDir::isAbsolutePath(g) || !QDir(g).exists()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The chosen location for dumped games is not valid."));
        return false;
    }

    if (a.isEmpty() || !QDir::isAbsolutePath(a)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The chosen location for dumped DLCs is not valid."));
        return false;
    }
    QDir dlc_dir(a);
    if (!dlc_dir.exists() && !dlc_dir.mkpath(".")) {
        QMessageBox::critical(this, tr("Error"), tr("The DLC dump location could not be created."));
        return false;
    }

    if (v.isEmpty() || !QDir::isAbsolutePath(v)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The location for installing emulator versions is not valid."));
        return false;
    }
    QDir version_dir(v);
    if (!version_dir.exists() && !version_dir.mkpath(".")) {
        QMessageBox::critical(this, tr("Error"),
                              tr("The emulator version location could not be created."));
        return false;
    }

    const auto games_path = GUI::Utils::NormalizePath(Common::FS::PathFromQString(g));
    const auto dlc_path = GUI::Utils::NormalizePath(Common::FS::PathFromQString(a));
    const auto version_path = GUI::Utils::NormalizePath(Common::FS::PathFromQString(v));

    m_emu_settings->AddGameInstallDir(std::filesystem::path(games_path));
    m_emu_settings->SetAddonInstallDir(std::filesystem::path(dlc_path));
    m_gui_settings->SetValue(GUI::version_manager_versionPath,
                             QString::fromStdString(version_path));
    m_emu_settings->Save();

    return true;
}

void SetupWizardFoldersPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
    }
    QWizardPage::changeEvent(event);
}

void SetupWizardFoldersPage::Retranslate() {
    setTitle(tr("Folders"));
    setSubTitle(
        tr("Tell shadLauncher4 where to find your games and where to put everything else."));

    m_games_label->setText(tr("Games:"));
    m_dlc_label->setText(tr("DLC:"));
    m_versions_label->setText(tr("Emulator versions:"));

    m_games_browse->setText(tr("Browse..."));
    m_dlc_browse->setText(tr("Browse..."));
    m_versions_browse->setText(tr("Browse..."));

    m_hint_label->setText(tr("The games folder must already exist. The DLC and emulator version "
                             "folders are created if they are missing. More library folders can "
                             "be added later under Settings - Paths."));
}

// Page 3 - emulator version
SetupWizardVersionPage::SetupWizardVersionPage(std::shared_ptr<GUISettings> gui_settings,
                                               QWidget* parent)
    : QWizardPage(parent), m_gui_settings(std::move(gui_settings)) {

    auto* layout = new QVBoxLayout(this);

    m_info_label = new QLabel(this);
    m_info_label->setWordWrap(true);
    layout->addWidget(m_info_label);

    m_version_list = new QListWidget(this);
    m_version_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_version_list, 1);

    m_empty_label = new QLabel(this);
    m_empty_label->setWordWrap(true);
    layout->addWidget(m_empty_label);

    auto* button_layout = new QHBoxLayout();
    m_manage_button = new QPushButton(this);
    button_layout->addWidget(m_manage_button);
    button_layout->addStretch();
    layout->addLayout(button_layout);

    connect(m_manage_button, &QPushButton::clicked, this,
            &SetupWizardVersionPage::OpenVersionManager);

    Retranslate();
}

void SetupWizardVersionPage::initializePage() {
    RefreshVersionList();
}

void SetupWizardVersionPage::RefreshVersionList() {
    m_version_list->clear();

    const QString selected =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();

    const auto versions = VersionManager::GetVersionList();
    for (const auto& version : versions) {
        const QString name = QString::fromStdString(version.name);
        const QString path = QString::fromStdString(version.path);

        auto* item = new QListWidgetItem(
            QStringLiteral("%1  -  %2").arg(name, VersionTypeName(version.type)), m_version_list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);

        if (!selected.isEmpty() && selected == path) {
            m_version_list->setCurrentItem(item);
        }
    }

    if (m_version_list->currentItem() == nullptr && m_version_list->count() > 0) {
        m_version_list->setCurrentRow(0);
    }

    const bool empty = m_version_list->count() == 0;
    m_version_list->setVisible(!empty);
    m_empty_label->setVisible(empty);
}

void SetupWizardVersionPage::OpenVersionManager() {
    VersionDialog dialog(m_gui_settings, this);
    dialog.exec();
    RefreshVersionList();
}

bool SetupWizardVersionPage::validatePage() {
    if (auto* item = m_version_list->currentItem()) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            m_gui_settings->SetValue(GUI::version_manager_versionSelected, path);
        }
    }
    return true;
}

void SetupWizardVersionPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
    }
    QWizardPage::changeEvent(event);
}

void SetupWizardVersionPage::Retranslate() {
    setTitle(tr("Emulator version"));
    setSubTitle(tr("shadLauncher4 runs your games with shadPS4. Pick the version to use."));

    m_info_label->setText(tr("Select an installed version below, or open the Version Manager to "
                             "download one."));
    m_empty_label->setText(tr("No emulator versions are installed yet. Open the Version Manager to "
                              "download one - you can also do this later from the main window."));
    m_manage_button->setText(tr("Open Version Manager..."));
}

// Page 4 - summary
SetupWizardConclusionPage::SetupWizardConclusionPage(
    std::shared_ptr<GUISettings> gui_settings, std::shared_ptr<EmulatorSettingsImpl> emu_settings,
    QWidget* parent)
    : QWizardPage(parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emu_settings)) {

    auto* layout = new QVBoxLayout(this);

    m_done_label = new QLabel(this);
    m_done_label->setWordWrap(true);
    layout->addWidget(m_done_label);
    layout->addSpacing(12);

    m_summary_label = new QLabel(this);
    m_summary_label->setWordWrap(true);
    m_summary_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary_label);
    layout->addStretch();

    Retranslate();
}

void SetupWizardConclusionPage::initializePage() {
    Retranslate();
}

void SetupWizardConclusionPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
    }
    QWizardPage::changeEvent(event);
}

void SetupWizardConclusionPage::Retranslate() {
    setTitle(tr("All set"));
    setSubTitle(tr("Review your choices and finish."));

    m_done_label->setText(tr("shadLauncher4 is ready to use. Everything below can be changed later "
                             "in Settings."));

    QString games = tr("(none)");
    const auto install_dirs = m_emu_settings->GetGameInstallDirs();
    if (!install_dirs.empty()) {
        Common::FS::PathToQString(games, install_dirs.front());
    }

    QString dlc;
    Common::FS::PathToQString(dlc, m_emu_settings->GetAddonInstallDir());

    QString versions = m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();
    if (versions.isEmpty()) {
        versions = tr("(none)");
    }

    QString selected_version =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    if (selected_version.isEmpty()) {
        selected_version = tr("(none selected)");
    }

    m_summary_label->setText(QStringLiteral("%1 %2\n%3 %4\n%5 %6\n%7 %8")
                                 .arg(tr("Games:"), games)
                                 .arg(tr("DLC:"), dlc)
                                 .arg(tr("Emulator versions:"), versions)
                                 .arg(tr("Selected version:"), selected_version));
}
