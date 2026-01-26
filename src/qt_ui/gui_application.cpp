// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDebug>
#include <QStyleHints>
#include <core/libraries/system/system_service.h>
#include <qfontdatabase.h>
#include <qlibraryinfo.h>
#include <qregularexpression.h>
#include <qstyle.h>
#include <qstylefactory.h>
#include "common/key_manager.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/ipc/ipc_client.h"
#include "game_install_dialog.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "main_window.h"
#include "persistent_settings.h"
#include "stylesheets.h"

s32 GUIApplication::m_language_id =
    static_cast<s32>(Libraries::SystemService::ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US);

GUIApplication::GUIApplication(int& argc, char** argv) : QApplication(argc, argv) {
    std::setlocale(LC_NUMERIC,
                   "C"); // On linux Qt changes to system locale while initializing QCoreApplication
}

GUIApplication::~GUIApplication() {}

bool GUIApplication::init() {

    m_gui_settings = std::make_shared<GUISettings>();
    m_emu_settings = std::make_shared<EmulatorSettings>();
    m_emu_settings->Load();
    m_persistent_settings = std::make_shared<PersistentSettings>();
    m_ipc_client = std::make_shared<IpcClient>();
    m_emu_state = std::make_shared<EmulatorState>();
    EmulatorState::SetInstance(m_emu_state);
    EmulatorSettings::SetInstance(m_emu_settings); // initialize singleton instance
    std::shared_ptr<KeyManager> m_key_manager = std::make_shared<KeyManager>();
    KeyManager::SetInstance(m_key_manager); // initialize singleton instance
    m_key_manager->LoadFromFile();          // load keys

    m_main_window = new MainWindow(m_gui_settings, m_emu_settings, m_persistent_settings,
                                   m_ipc_client, nullptr);

    const auto codes = getAvailableLanguageCodes();
    const auto language = m_gui_settings->GetValue(GUI::localization_language).toString();
    const auto index = codes.indexOf(language);

    loadLanguage(index < 0 ? QLocale(QLocale::English, QLocale::UnitedStates).bcp47Name()
                           : codes.at(index));

    // Create connects to propagate events throughout Gui.
    InitializeConnects();

    if (m_emu_settings->GetGameInstallDirs().empty()) {
        GameInstallDialog dlg(m_gui_settings, m_emu_settings);
        dlg.exec();
    }

    m_main_window->init();

    return true;
}

void GUIApplication::switchTranslator(QTranslator& translator, const QString& filename,
                                      const QString& language_code) {
    // remove the old translator
    removeTranslator(&translator);

    const QString lang_path = ":/translations/";
    const QString file_path = lang_path + filename;

    if (QFileInfo(file_path).isFile()) {
        // load the new translator
        if (translator.load(file_path)) {
            installTranslator(&translator);
        }
    } else if (QString default_code = QLocale(QLocale::English, QLocale::UnitedStates).bcp47Name();
               language_code != default_code) {
        // show error, but ignore default case "en", since it is handled in source code
        qDebug() << "No Translation file not found:" << file_path;

        // reset current language to default "en"
        setLanguageCode(std::move(default_code));
    }
}

void GUIApplication::loadLanguage(const QString& language_code) {
    if (m_language_code == language_code) {
        return;
    }

    setLanguageCode(language_code);

    const QLocale locale = QLocale(language_code);
    const QString locale_name = QLocale::languageToString(locale.language());

    QLocale::setDefault(locale);

    // Idk if this is overruled by the QLocale default, so I'll change it here just to be sure.
    // As per QT recommendations to avoid conflicts for POSIX functions
    std::setlocale(LC_NUMERIC, "C");

    switchTranslator(m_translator, QStringLiteral("%1.qm").arg(language_code), language_code);

    if (m_main_window) {
        const QString default_code = QLocale(QLocale::English, QLocale::UnitedStates).name();
        QStringList language_codes = getAvailableLanguageCodes();

        if (!language_codes.contains(default_code)) {
            language_codes.prepend(default_code);
        } else {
            language_codes.removeAll(default_code);
            // Insert the default at the top
            language_codes.prepend(default_code);
        }

        m_main_window->retranslateUI(language_codes, m_language_code);
    }

    m_gui_settings->SetValue(GUI::localization_language, m_language_code);

    qDebug() << "Current language changed to" << locale_name << "(" << language_code << ")";
    EmulatorSettings::GetInstance()->SetConsoleLanguage(m_language_id);
    EmulatorSettings::GetInstance()->Save();
}

QStringList GUIApplication::getAvailableLanguageCodes() {
    QStringList language_codes;

    const QString language_path = ":/translations/";

    if (QFileInfo(language_path).isDir()) {
        const QDir dir(language_path);
        const QStringList filenames = dir.entryList(QStringList("*.qm"));

        for (const QString& filename : filenames) {
            QString language_code = filename;                       // "en.qm"
            language_code.truncate(language_code.lastIndexOf('.')); // "en"

            if (language_codes.contains(language_code)) {
                qDebug() << "Found duplicate language:" << language_code << "(" << filename << ")";
            } else {
                language_codes << language_code;
            }
        }
    }

    return language_codes;
}

void GUIApplication::setLanguageCode(QString language_code) {
    m_language_code = language_code;

    // Transform language code to lowercase and use '-'
    language_code = language_code.toLower().replace("_", "-");

    // Try to find the CELL language ID for this language code
    using namespace Libraries::SystemService;
    static const std::map<QString, s32> language_ids = {
        {"ja", ORBIS_SYSTEM_PARAM_LANG_JAPANESE},
        {"ja-jp", ORBIS_SYSTEM_PARAM_LANG_JAPANESE},
        {"en", ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US},
        {"en-us", ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US},
        {"en-gb", ORBIS_SYSTEM_PARAM_LANG_ENGLISH_GB},
        {"fr", ORBIS_SYSTEM_PARAM_LANG_FRENCH},
        {"es", ORBIS_SYSTEM_PARAM_LANG_SPANISH},
        {"es-es", ORBIS_SYSTEM_PARAM_LANG_SPANISH},
        {"de", ORBIS_SYSTEM_PARAM_LANG_GERMAN},
        {"it", ORBIS_SYSTEM_PARAM_LANG_ITALIAN},
        {"nl", ORBIS_SYSTEM_PARAM_LANG_DUTCH},
        {"pt", ORBIS_SYSTEM_PARAM_LANG_PORTUGUESE_PT},
        {"pt-pt", ORBIS_SYSTEM_PARAM_LANG_PORTUGUESE_PT},
        {"pt-br", ORBIS_SYSTEM_PARAM_LANG_PORTUGUESE_BR},
        {"ru", ORBIS_SYSTEM_PARAM_LANG_RUSSIAN},
        {"ko", ORBIS_SYSTEM_PARAM_LANG_KOREAN},
        {"zh", ORBIS_SYSTEM_PARAM_LANG_CHINESE_T},
        {"zh-hant", ORBIS_SYSTEM_PARAM_LANG_CHINESE_T},
        {"zh-hans", ORBIS_SYSTEM_PARAM_LANG_CHINESE_S},
        {"fi", ORBIS_SYSTEM_PARAM_LANG_FINNISH},
        {"sv", ORBIS_SYSTEM_PARAM_LANG_SWEDISH},
        {"da", ORBIS_SYSTEM_PARAM_LANG_DANISH},
        {"no", ORBIS_SYSTEM_PARAM_LANG_NORWEGIAN},
        {"nn", ORBIS_SYSTEM_PARAM_LANG_NORWEGIAN},
        {"nb", ORBIS_SYSTEM_PARAM_LANG_NORWEGIAN},
        {"pl", ORBIS_SYSTEM_PARAM_LANG_POLISH},
        {"tr", ORBIS_SYSTEM_PARAM_LANG_TURKISH},
        {"tr-tr", ORBIS_SYSTEM_PARAM_LANG_TURKISH},
        {"es-419", ORBIS_SYSTEM_PARAM_LANG_SPANISH_LA},
        {"ar-ae", ORBIS_SYSTEM_PARAM_LANG_ARABIC},
        {"ar", ORBIS_SYSTEM_PARAM_LANG_ARABIC},
        {"fr-ca", ORBIS_SYSTEM_PARAM_LANG_FRENCH_CA},
        {"cs", ORBIS_SYSTEM_PARAM_LANG_CZECH},
        {"cs-cz", ORBIS_SYSTEM_PARAM_LANG_CZECH},
        {"hu-hu", ORBIS_SYSTEM_PARAM_LANG_HUNGARIAN},
        {"hu", ORBIS_SYSTEM_PARAM_LANG_HUNGARIAN},
        {"el-gr", ORBIS_SYSTEM_PARAM_LANG_GREEK},
        {"el", ORBIS_SYSTEM_PARAM_LANG_GREEK},
        {"ro-ro", ORBIS_SYSTEM_PARAM_LANG_ROMANIAN},
        {"ro", ORBIS_SYSTEM_PARAM_LANG_ROMANIAN},
        {"th-th", ORBIS_SYSTEM_PARAM_LANG_THAI},
        {"th", ORBIS_SYSTEM_PARAM_LANG_THAI},
        {"vi-vn", ORBIS_SYSTEM_PARAM_LANG_VIETNAMESE},
        {"vi", ORBIS_SYSTEM_PARAM_LANG_VIETNAMESE},
        {"id-id", ORBIS_SYSTEM_PARAM_LANG_INDONESIAN},
        {"id", ORBIS_SYSTEM_PARAM_LANG_INDONESIAN},
        {"uk", ORBIS_SYSTEM_PARAM_LANG_UKRAINIAN},

    };

    // Check direct match first
    const auto it = language_ids.find(language_code);
    if (it != language_ids.cend()) {
        m_language_id = static_cast<s32>(it->second);
        return;
    }

    // Try to find closest match
    for (const auto& [code, id] : language_ids) {
        if (language_code.startsWith(code)) {
            m_language_id = static_cast<s32>(id);
            return;
        }
    }

    // Fallback to English (US)
    m_language_id = static_cast<s32>(ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US);
}

s32 GUIApplication::getLanguageId() {
    return m_language_id;
}

void GUIApplication::InitializeConnects() {
    if (m_main_window) {
        connect(m_main_window, &MainWindow::requestLanguageChange, this,
                &GUIApplication::loadLanguage);
        connect(m_main_window, &MainWindow::RequestGlobalStylesheetChange, this,
                &GUIApplication::OnChangeStyleSheetRequest);
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this]() { OnChangeStyleSheetRequest(); });
    }
}

/*
 * Handle a request to change the stylesheet based on the current entry in the settings.
 */
void GUIApplication::OnChangeStyleSheetRequest() {
    // Remove old fonts
    QFontDatabase::removeAllApplicationFonts();

    const QString stylesheet_name =
        m_gui_settings->GetValue(GUI::meta_currentStylesheet).toString();

    // Determine default style
    if (m_default_style.isEmpty()) {
#ifdef _WIN32
        // On windows, the custom stylesheets don't seem to work properly unless we use the
        // windowsvista style as default
        if (QStyleFactory::keys().contains("windowsvista")) {
            m_default_style = "windowsvista";
            // qDebug() << "Using 'windowsvista' as default style on Windows";
        }
#endif

        // Use the initial style as default style
        if (const QStyle* style = m_default_style.isEmpty() ? QApplication::style() : nullptr) {
            m_default_style = style->name();
            // qDebug() << "Determined" << m_default_style << "as default style";
        }

        // Fallback to the first style, which is supposed to be the default style according to the
        // Qt docs.
        if (m_default_style.isEmpty()) {
            if (const QStringList styles = QStyleFactory::keys(); !styles.empty()) {
                m_default_style = styles.front();
                // qDebug() << "Determined" << m_default_style
                //          << "as default style (first style available)";
            }
        }
    }

    // Reset style to default before doing anything else, or we will get unexpected effects in
    // custom stylesheets.
    if (QStyle* style = QStyleFactory::create(m_default_style)) {
        setStyle(style);
    }

    const auto match_native_style = [&stylesheet_name]() -> QString {
        // Search for "native (<style>)"
        static const QRegularExpression expr(GUI::NativeStylesheet + " \\((?<style>.*)\\)");
        const QRegularExpressionMatch match = expr.match(stylesheet_name);

        if (match.hasMatch()) {
            return match.captured("style");
        }

        return {};
    };
    // qDebug() << "Changing stylesheet to" << stylesheet_name;
    GUI::custom_stylesheet_active = false;

    if (stylesheet_name.isEmpty() || stylesheet_name == GUI::DefaultStylesheet) {
        // qDebug() << "Using default stylesheet";
        setStyleSheet(GUI::Stylesheets::default_style_sheet);
        GUI::custom_stylesheet_active = true;
    } else if (stylesheet_name == GUI::NoStylesheet) {
        // qDebug() << "Using no stylesheet";
        setStyleSheet("/* none */");
    } else if (const QString native_style = match_native_style(); !native_style.isEmpty()) {
        if (QStyle* style = QStyleFactory::create(native_style)) {
            // qDebug() << "Using native style:" << native_style;
            setStyleSheet("/* none */");
            setStyle(style);
        } else {
            // qDebug() << "Failed to set stylesheet: Native style" << native_style << "not
            // available";
        }
    } else {
        // qDebug() << "Using custom stylesheet:" << stylesheet_name;
    }

    GUI::stylesheet = styleSheet();

    if (m_main_window) {
        m_main_window->RepaintGUI();
    }
}