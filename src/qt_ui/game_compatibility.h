// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class GUISettings;
class Downloader;

namespace Compat {

struct Status {
    int index = 0;
    QString last_tested_date;
    QString color;
    QString text;
    QString tooltip;
    QString latest_version;
    QString issue_number;
};

} // namespace Compat

class GameCompatibility : public QObject {
    Q_OBJECT

private:
    /* clang-format off */
    const std::map<QString, Compat::Status> m_status_data =
	{
		{ "Playable", { 0, "", "#47D35C", tr("Playable"),         tr("Games that can be run without any major issues") } },
		{ "Ingame",   { 1, "", "#F2D624", tr("Ingame"),           tr("Games that can reach gameplay but have issues") } },
		{ "Menus",    { 2, "", "#FF0000", tr("Menus"),            tr("Games that can reach the menu but freeze/crash when trying to proceed further") } },
		{ "Boots",    { 3, "", "#828282", tr("Boots"),            tr("Games that show any visual/audio output but freeze/crash before reaching the menu") } },
		{ "Nothing",  { 4, "", "#212121", tr("Nothing"),          tr("Games that crash when trying to launch or only show a black screen") } },
		{ "NoResult", { 5, "", "",        tr("No results found"), tr("There is no entry for this game or application in the compatibility database yet.") } },
		{ "NoData",   { 6, "", "",        tr("Database missing"), tr("Right click here and choose Compatibility -> Update Database.") } },
		{ "Download", { 7, "", "",        tr("Retrieving..."),    tr("Downloading the compatibility database. Please wait...") } }
	};
    /* clang-format on */
    std::shared_ptr<GUISettings> m_gui_settings;
    std::map<std::string, Compat::Status> m_compat_database;
    QString m_filepath;
    /** Creates new map from the database */
    bool ReadJSON(const QJsonObject& json_data, bool after_download);
    QString NormalizeStatusString(const QString& value) {
        QString result = value;

        if (result.startsWith("status-"))
            result = result.mid(7);

        if (!result.isEmpty())
            result[0] = result[0].toUpper();

        return result;
    }
    Downloader* m_downloader = nullptr;

public:
    /** Handles reads, writes and downloads for the compatibility database */
    GameCompatibility(std::shared_ptr<GUISettings> gui_settings, QWidget* parent);
    /** Returns the compatibility status for the requested title */
    Compat::Status GetCompatibility(const std::string& title_id);
    /** Returns the data for the requested status */
    Compat::Status GetStatusData(const QString& status) const;
    /** Reads database. If online set to true: Downloads and writes the database to file */
    void RequestCompatibility(bool online = false);

Q_SIGNALS:
    void DownloadStarted();
    void DownloadFinished();
    void DownloadCanceled();
    void DownloadError(const QString& error);
private Q_SLOTS:
    void HandleDownloadError(const QString& error);
    void HandleDownloadFinished(const QByteArray& content);
    void HandleDownloadCanceled();
};
