// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    // Assembles a plain-text block (version, commit, branch, build date,
    // Qt version, OS, arch) suitable for pasting into a bug report.
    QString BuildInfoText() const;

    // Resolves the project's GitHub URL, preferring the compiled-in remote
    // (g_scm_remote_url) and falling back to the canonical repo link.
    static QString RepositoryUrl();
};
