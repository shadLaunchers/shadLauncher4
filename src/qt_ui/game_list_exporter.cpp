// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/path_util.h>
#include "game_list_exporter.h"

GameListExporter::GameListExporter(GameListFrame* gameListFrame, QWidget* parent)
    : QObject(parent), m_gameListFrame(gameListFrame), m_parent(parent) {}

void GameListExporter::ShowExportDialog() {
    QDialog dialog(m_parent);
    dialog.setWindowTitle(QObject::tr("Export Game List"));
    dialog.setModal(true);
    dialog.setFixedSize(360, 260);

    auto layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(QObject::tr("Select export format:"), &dialog));
    auto formatCombo = new QComboBox(&dialog);
    formatCombo->addItems({"Text (.txt)", "CSV (.csv)"});
    layout->addWidget(formatCombo);

    layout->addWidget(new QLabel(QObject::tr("Sort by:"), &dialog));
    auto sortCombo = new QComboBox(&dialog);
    sortCombo->addItems({"Name", "ID", "Firmware", "Sdk Version", "App Version", "Category",
                         "Region", "NP Comm IDs", "Path"});
    layout->addWidget(sortCombo);

    layout->addWidget(new QLabel(QObject::tr("Sort order:"), &dialog));
    auto orderCombo = new QComboBox(&dialog);
    orderCombo->addItems({"Ascending", "Descending"});
    layout->addWidget(orderCombo);

    auto includeCompatCheck = new QCheckBox(QObject::tr("Include compatibility info"), &dialog);
    layout->addWidget(includeCompatCheck);

    auto buttonLayout = new QHBoxLayout();
    auto okButton = new QPushButton(QObject::tr("Export"), &dialog);
    auto cancelButton = new QPushButton(QObject::tr("Cancel"), &dialog);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const bool asCsv = (formatCombo->currentIndex() == 1);
    const int sortIndex = sortCombo->currentIndex();
    const bool ascending = (orderCombo->currentIndex() == 0);

    QString defaultFileName = asCsv ? "GameList.csv" : "GameList.txt";
    QString selectedFilter = asCsv ? "CSV Files (*.csv)" : "Text Files (*.txt)";
    QString filePath =
        QFileDialog::getSaveFileName(m_parent, QObject::tr("Save Game List As"),
                                     QDir(qApp->applicationDirPath()).filePath(defaultFileName),
                                     "Text Files (*.txt);;CSV Files (*.csv)", &selectedFilter);

    if (filePath.isEmpty())
        return;

    if (asCsv && !filePath.endsWith(".csv", Qt::CaseInsensitive))
        filePath += ".csv";
    else if (!asCsv && !filePath.endsWith(".txt", Qt::CaseInsensitive))
        filePath += ".txt";

    ExportToFile(asCsv, sortIndex, ascending, filePath, includeCompatCheck->isChecked());
}

void GameListExporter::ExportToFile(bool asCsv, int sortIndex, bool ascending,
                                    const QString& filePath, bool includeCompat) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(m_parent, QObject::tr("Error"),
                             QObject::tr("Failed to create file:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);

    std::vector<game_info> sortedGames = m_gameListFrame->GetGameInfo();

    std::sort(
        sortedGames.begin(), sortedGames.end(),
        [sortIndex, ascending](const game_info& a, const game_info& b) {
            const GameInfo& ga = a->info;
            const GameInfo& gb = b->info;

            auto cmp = [&](const QString& va, const QString& vb) {
                return ascending ? va.localeAwareCompare(vb) < 0 : va.localeAwareCompare(vb) > 0;
            };

            switch (sortIndex) {
            case 0:
                return cmp(QString::fromStdString(ga.name), QString::fromStdString(gb.name));
            case 1:
                return cmp(QString::fromStdString(ga.serial), QString::fromStdString(gb.serial));
            case 2:
                return cmp(QString::fromStdString(ga.fw), QString::fromStdString(gb.fw));
            case 3:
                return cmp(QString::fromStdString(ga.app_ver), QString::fromStdString(gb.app_ver));
            case 4:
                return cmp(QString::fromStdString(ga.sdk_ver), QString::fromStdString(gb.sdk_ver));
            case 5:
                return cmp(QString::fromStdString(ga.category),
                           QString::fromStdString(gb.category));
            case 6:
                return cmp(QString::fromStdString(ga.region), QString::fromStdString(gb.region));
            case 7: {
                auto joinNpCommIds = [](const std::vector<std::string>& ids) -> QString {
                    if (ids.empty())
                        return "";
                    QString result = QString::fromStdString(ids[0]);
                    for (size_t i = 1; i < ids.size(); ++i) {
                        result += "," + QString::fromStdString(ids[i]);
                    }
                    return result;
                };
                return cmp(joinNpCommIds(ga.np_comm_ids), joinNpCommIds(gb.np_comm_ids));
            }
            case 8:
                return cmp(QString::fromStdString(ga.path), QString::fromStdString(gb.path));
            default:
                return false;
            }
        });

    if (asCsv)
        WriteCsv(out, sortedGames, sortIndex, ascending, includeCompat);
    else
        WriteTxt(out, sortedGames, sortIndex, ascending, includeCompat);

    file.close();

    QMessageBox::information(m_parent, QObject::tr("Game List Exported"),
                             QObject::tr("Game list successfully exported to:\n%1").arg(filePath));
}

QString GameListExporter::EscapeCsv(const QString& s) {
    QString escaped = s;
    escaped.replace("\"", "\"\"");
    return "\"" + escaped + "\"";
}

void GameListExporter::WriteTxt(QTextStream& out, const std::vector<game_info>& games,
                                int sortIndex, bool ascending, bool includeCompat) {
    QProgressDialog progressDlg("Exporting game list...", "Cancel", 0,
                                static_cast<int>(games.size()));
    progressDlg.setWindowModality(Qt::ApplicationModal);
    progressDlg.setMinimumDuration(0);

    struct QCharRange {
        ushort first;
        ushort last;
    };
    auto CharWidth = [](QChar c) -> int {
        static const QCharRange wide[] = {{0x1100, 0x115F}, {0x2329, 0x232A}, {0x2E80, 0x2FFF},
                                          {0x3000, 0x303E}, {0x3040, 0x309F}, {0x30A0, 0x30FF},
                                          {0x3100, 0x312F}, {0x3130, 0x318F}, {0x3190, 0x31EF},
                                          {0x3200, 0x4DBF}, {0x4E00, 0xA4CF}, {0xA960, 0xA97F},
                                          {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19},
                                          {0xFE30, 0xFE6F}, {0xFF00, 0xFFEF}};
        ushort code = c.unicode();
        for (auto r : wide)
            if (code >= r.first && code <= r.last)
                return 2;
        return 1;
    };

    auto StringWidth = [&](const QString& s) {
        int w = 0;
        for (QChar c : s)
            w += CharWidth(c);
        return w;
    };

    auto TruncateToWidth = [&](const QString& s, int maxWidth) {
        if (StringWidth(s) <= maxWidth)
            return s; // keep short strings
        int w = 0;
        QString outStr;
        for (QChar c : s) {
            int cw = CharWidth(c);
            if (w + cw > maxWidth - 3)
                break;
            outStr.append(c);
            w += cw;
        }
        return outStr + "...";
    };

    auto FormatColumn = [&](const QString& text, int width, Qt::Alignment align) {
        QString t = TruncateToWidth(text, width);
        int pad = width - StringWidth(t);
        return (align == Qt::AlignRight) ? QString(" ").repeated(pad) + t
                                         : t + QString(" ").repeated(pad);
    };
    QStringList headers = {"NAME",        "Serial",   "FW",     "App Version",
                           "SDK Version", "Category", "Region", "NP Comm IDs"};
    if (includeCompat) {
        headers << "Status" << "Last Version" << "Last Tested" << "Issue#";
    }
    headers << "Path";

    QVector<QStringList> rows;
    rows.reserve(games.size());

    for (size_t i = 0; i < games.size(); ++i) {
        if (progressDlg.wasCanceled())
            return;

        const auto& g = games[i];

        QString qpath;
        Common::FS::PathToQString(qpath, g->info.path);

        auto joinNpCommIds = [](const std::vector<std::string>& ids) -> QString {
            if (ids.empty())
                return "";
            QString result = QString::fromStdString(ids[0]);
            for (size_t i = 1; i < ids.size(); ++i) {
                result += "," + QString::fromStdString(ids[i]);
            }
            return result;
        };
        QStringList row = {
            QString::fromStdString(g->info.name),    QString::fromStdString(g->info.serial),
            QString::fromStdString(g->info.fw),      QString::fromStdString(g->info.app_ver),
            QString::fromStdString(g->info.sdk_ver), QString::fromStdString(g->info.category),
            QString::fromStdString(g->info.region),  joinNpCommIds(g->info.np_comm_ids)};

        if (includeCompat) {
            row << g->compat.text << g->compat.latest_version << g->compat.last_tested_date
                << g->compat.issue_number;
        }

        row << qpath;
        rows.push_back(row);

        progressDlg.setValue(static_cast<int>(i));
        qApp->processEvents();
    }

    const int MAX_CELL_WIDTH = 128;
    QVector<int> colWidths(headers.size(), 0);
    for (int col = 0; col < headers.size(); ++col) {
        int maxWidth = StringWidth(headers[col]); // start with header
        for (const auto& r : rows)
            maxWidth = std::max(maxWidth, StringWidth(r[col]));
        colWidths[col] = std::min(maxWidth + 2, MAX_CELL_WIDTH); // padding + max cap
    }

    QVector<Qt::Alignment> aligns;
    aligns << Qt::AlignLeft  // NAME
           << Qt::AlignLeft  // Serial
           << Qt::AlignRight // FW
           << Qt::AlignRight // App Version
           << Qt::AlignRight // SDK Version
           << Qt::AlignLeft  // Category
           << Qt::AlignLeft  // Region
           << Qt::AlignLeft; // NP Comm IDs

    if (includeCompat) {
        aligns << Qt::AlignLeft   // Status
               << Qt::AlignRight  // Last Version
               << Qt::AlignRight  // Last Tested
               << Qt::AlignRight; // Issue#
    }

    aligns << Qt::AlignLeft; // Path

    auto PrintRow = [&](const QStringList& cols) {
        for (int c = 0; c < cols.size(); ++c)
            out << FormatColumn(cols[c], colWidths[c], aligns[c]) << " ";
        out << "\n";
    };

    PrintRow(headers);

    int lineLen = 0;
    for (int w : colWidths)
        lineLen += w + 1;

    out << QString("-").repeated(lineLen) << "\n";

    for (int i = 0; i < rows.size(); ++i) {
        if (progressDlg.wasCanceled())
            return;
        PrintRow(rows[i]);
        progressDlg.setValue(i);
        qApp->processEvents();
    }

    progressDlg.setValue(static_cast<int>(games.size()));
}

void GameListExporter::WriteCsv(QTextStream& out, const std::vector<game_info>& sortedGames,
                                int sortIndex, bool ascending, bool includeCompat) {
    if (sortedGames.empty())
        return;

    auto EscapeCsv = [](const QString& s) -> QString {
        QString escaped = s;
        escaped.replace("\"", "\"\"");
        return "\"" + escaped + "\"";
    };

    auto joinNpCommIds = [](const std::vector<std::string>& ids) -> QString {
        if (ids.empty())
            return "";
        QString result = QString::fromStdString(ids[0]);
        for (size_t i = 1; i < ids.size(); ++i) {
            result += "," + QString::fromStdString(ids[i]);
        }
        return result;
    };

    // --- Define headers dynamically ---
    QStringList headers = {"Name",    "Serial",   "Firmware", "App Version",
                           "SDK Ver", "Category", "Region",   "NP Comm IDs"};
    if (includeCompat) {
        headers << "Status" << "Last Version" << "Last Tested" << "Issue#";
    }
    headers << "Path";

    // Write header
    out << headers.join(",") << "\n";

    // --- Progress dialog ---
    ProgressDialog progressDlg(QObject::tr("Exporting Game List"),
                               QObject::tr("Exporting games to CSV..."), QObject::tr("Cancel"), 0,
                               static_cast<int>(sortedGames.size()), false, m_parent);
    progressDlg.show();

    // --- Write rows ---
    for (int i = 0; i < sortedGames.size(); ++i) {
        const auto& gptr = sortedGames[i];
        const auto& info = gptr->info;
        const auto& status = gptr->compat;

        QString game_path;
        Common::FS::PathToQString(game_path, info.path);

        QStringList row = {EscapeCsv(QString::fromStdString(info.name)),
                           EscapeCsv(QString::fromStdString(info.serial)),
                           EscapeCsv(QString::fromStdString(info.fw)),
                           EscapeCsv(QString::fromStdString(info.app_ver)),
                           EscapeCsv(QString::fromStdString(info.sdk_ver)),
                           EscapeCsv(QString::fromStdString(info.category)),
                           EscapeCsv(QString::fromStdString(info.region)),
                           EscapeCsv(joinNpCommIds(info.np_comm_ids))};

        if (includeCompat) {
            row << EscapeCsv(status.text) << EscapeCsv(status.latest_version)
                << EscapeCsv(status.last_tested_date) << EscapeCsv(status.issue_number);
        }

        row << EscapeCsv(game_path);

        out << row.join(",") << "\n";

        progressDlg.SetValue(i + 1);
        qApp->processEvents();
        if (progressDlg.wasCanceled())
            break;
    }

    progressDlg.close();
}
