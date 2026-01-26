// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include "game_list_frame.h"
#include "progress_dialog.h"

class GameListExporter : public QObject {
    Q_OBJECT

public:
    explicit GameListExporter(GameListFrame* gameListFrame, QWidget* parent = nullptr);

    void ShowExportDialog();

private:
    static QString EscapeCsv(const QString& s);
    void ExportToFile(bool asCsv, int sortIndex, bool ascending, const QString& filePath,
                      bool includeCompat);
    void WriteTxt(QTextStream& out, const std::vector<game_info>& games, int sortIndex,
                  bool ascending, bool includeCompat);
    void WriteCsv(QTextStream& out, const std::vector<game_info>& games, int sortIndex,
                  bool ascending, bool includeCompat);

private:
    GameListFrame* m_gameListFrame = nullptr;
    QWidget* m_parent = nullptr;
    int m_title_width = 60; // adjust to avoid truncate on big titles if needed
};