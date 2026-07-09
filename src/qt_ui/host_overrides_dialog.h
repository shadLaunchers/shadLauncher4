// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <QDialog>

class QTableWidget;
class QLabel;

// Editor for the HTTP host-overrides file (host_overrides.json in the user
// directory). The emulator's libSceHttp reads this file to redirect game
// network traffic to a different endpoint (e.g. a local private server).
//
// File format (see http.cpp): a flat JSON object mapping a match key to a
// redirect target. Keys are matched most-specific first:
//   "scheme://host:port"  exact endpoint
//   "host:port"           host+port, any scheme
//   "host"                host, any scheme/port (most common)
//   "*"                   catch-all
// Keys beginning with '_' are treated as comments and ignored by the loader;
// this dialog uses that to represent disabled entries.
class HostOverridesDialog : public QDialog {
    Q_OBJECT
public:
    explicit HostOverridesDialog(QWidget* parent = nullptr);

    static std::filesystem::path FilePath();

private:
    void AddRow(const QString& match, const QString& target, bool enabled);
    void OnAdd();
    void OnRemove();
    void OnAddCatchAll();
    void OnImport();
    void OnExport();
    void LoadFromDisk();
    bool SaveToDisk();

    QTableWidget* m_table = nullptr;
    QLabel* m_path_label = nullptr;
};
