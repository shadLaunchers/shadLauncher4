// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <QApplication>
#include <QMessageBox>

#include "common/logging/log.h"
#include "qt_ui/gui_application.h"
#include "qt_ui/stylesheets.h"

int main(int argc, char* argv[]) {
    // Start default log
    Common::Log::Setup("shadLauncher4.log");

    QScopedPointer<QCoreApplication> app(new GUIApplication(argc, argv));
    GUIApplication* gui_app = qobject_cast<GUIApplication*>(app.data());

    QStringList passed_args{};
    QString emulator_arg = "";
    QString game_arg = "";

    // Map of argument strings to lambda functions
    std::unordered_map<std::string, std::function<void(int&)>> arg_map = {
        {"-h",
         [&](int&) {
             QString helpMsg =
                 "Usage: shadps4 [options]\n"
                 "Options:\n"
                 "  No arguments: Opens the GUI.\n"
                 "  -e, --emulator <name|path>    Specify the emulator version/path you want to "
                 "use, or 'default' for using the version selected in the config.\n"
                 "  -g, --game <ID|path>          Specify game to launch.\n"
                 "  -d                            Alias for '-e default'.\n"
                 "  -h, --help                    Display this help message.\n"
                 " -- ...                         Parameters passed to the emulator core.";
             QMessageBox::information(nullptr, "tr(shadLauncher4 command line options)", helpMsg);
             exit(0);
         }},

        {"--help", [&](int& i) { arg_map["-h"](i); }}, // Redirect --help to -h
        {"-g",
         [&](int& i) {
             if (i + 1 < argc) {
                 game_arg = argv[++i];
             } else {
                 std::cerr << "Error: Missing argument for -g/--game\n";
                 exit(1);
             }
         }},
        {"--game", [&](int& i) { arg_map["-g"](i); }},
        {"-e",
         [&](int& i) {
             if (i + 1 < argc) {
                 emulator_arg = argv[++i];
             } else {
                 std::cerr << "Error: Missing argument for -e/--emulator\n";
                 exit(1);
             }
         }},
        {"--emulator", [&](int& i) { arg_map["-e"](i); }},
        {"-d", [&](int&) { emulator_arg = "default"; }},
    };

    // Parse command-line arguments using the map
    for (int i = 1; i < argc; ++i) {
        std::string cur_arg = argv[i];
        auto it = arg_map.find(cur_arg);
        if (it != arg_map.end()) {
            it->second(i); // Call the associated lambda function
        } else if (std::string(argv[i]) == "--") {
            if (i + 1 == argc) {
                std::cerr << "Warning: -- is set, but no emulator arguments are added!\n";
                break;
            }
            for (int j = i + 1; j < argc; j++) {
                passed_args.push_back(argv[j]);
            }
            break;
        } else {
            std::cerr << "Unknown argument: " << cur_arg << ", see --help for info.\n";
            return 1;
        }
    }

    qApp->setStyleSheet(GUI::Stylesheets::default_style_sheet);
    gui_app->init(emulator_arg, game_arg);
    return app->exec();
}
