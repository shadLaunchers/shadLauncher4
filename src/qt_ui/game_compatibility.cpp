// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/path_util.h>
#include "downloader.h"
#include "game_compatibility.h"
#include "gui_settings.h"

#include <QJsonArray>
#include <QJsonDocument>

GameCompatibility::GameCompatibility(std::shared_ptr<GUISettings> gui_settings, QWidget* parent)
    : QObject(parent), m_gui_settings(std::move(gui_settings)) {
    const std::filesystem::path compat_path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "compatibility_data.json";
#ifdef _WIN32
    m_filepath = QString::fromStdWString(compat_path.wstring()); // UTF-16 Windows
#else
    m_filepath = QString::fromUtf8(compat_path.u8string().c_str()); // UTF-8 Linux/macOS
#endif

    m_downloader = new Downloader(m_gui_settings, GUI::compatibility_etag,
                                  GUI::compatibility_last_modified, parent);
    RequestCompatibility();

    connect(m_downloader, &Downloader::SignalDownloadError, this,
            &GameCompatibility::HandleDownloadError);
    connect(m_downloader, &Downloader::SignalDownloadFinished, this,
            &GameCompatibility::HandleDownloadFinished);
    connect(m_downloader, &Downloader::SignalDownloadCanceled, this,
            &GameCompatibility::HandleDownloadCanceled);
}

Compat::Status GameCompatibility::GetCompatibility(const std::string& title_id) {
    if (m_compat_database.empty()) {
        return m_status_data.at("NoData");
    }

    if (const auto it = m_compat_database.find(title_id); it != m_compat_database.cend()) {
        return it->second;
    }

    return m_status_data.at("NoResult");
}

Compat::Status GameCompatibility::GetStatusData(const QString& status) const {
    return m_status_data.at(status);
}

void GameCompatibility::HandleDownloadFinished(const QByteArray& content) {
    qDebug() << "Database download finished";

    // Create new map from database and write database to file if database was valid
    if (ReadJSON(QJsonDocument::fromJson(content).object(), true)) {
        // Write database to file
        QFile file(m_filepath);

        if (file.exists()) {
            qDebug() << "Database file found:" << m_filepath;
        }

        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Could not write database to file:" << m_filepath;
            return;
        }

        file.write(content);
        file.close();

        qDebug() << "Wrote database to file:" << m_filepath;
    }

    // We have a new database in map, therefore refresh gamelist to new state
    Q_EMIT DownloadFinished();
}

void GameCompatibility::HandleDownloadCanceled() {
    Q_EMIT DownloadCanceled();
}

void GameCompatibility::HandleDownloadError(const QString& error) {
    Q_EMIT DownloadError(error);
}

void GameCompatibility::RequestCompatibility(bool online) {
    if (!online) {
        // Retrieve database from file
        QFile file(m_filepath);

        if (!file.exists()) {
            qDebug() << "Database file not found:" << m_filepath;
            return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Could not read database from file:" << m_filepath;
            return;
        }

        const QByteArray content = file.readAll();
        file.close();

        qDebug() << "Finished reading database from file:" << m_filepath;

        // Create new map from database
        ReadJSON(QJsonDocument::fromJson(content).object(), online);

        return;
    }
    const std::string url =
        m_gui_settings->GetValue(GUI::compatibility_json_url).toString().toStdString();
    qDebug() << "Beginning compatibility database download from:" << QString::fromStdString(url);

    m_downloader->DownloadJSONWithCache(url, m_filepath, true,
                                        tr("Downloading Compatibility Database"));

    // We want to retrieve a new database, therefore refresh gamelist and indicate that
    Q_EMIT DownloadStarted();
}

bool GameCompatibility::ReadJSON(const QJsonObject& json_data, bool after_download) {
    // Set current_os automatically
    QString current_os;
#ifdef Q_OS_WIN
    current_os = "os-windows";
#elif defined(Q_OS_MAC)
    current_os = "os-macOS";
#elif defined(Q_OS_LINUX)
    current_os = "os-linux";
#else
    current_os = "os-unknown";
#endif
    if (json_data.isEmpty()) {
        qDebug() << "Database Error - Empty JSON root";
        Q_EMIT DownloadError(tr("Error Downloading Compatibility Database"));
        return false;
    }
    m_compat_database.clear();
    for (const QString& game_id : json_data.keys()) {
        const QJsonValue game_value = json_data.value(game_id);
        if (!game_value.isObject()) {
            qDebug() << "Database Error - Unusable object:" << game_id;
            continue;
        }
        const QJsonObject game_object = game_value.toObject();
        for (const QString& platform_key : game_object.keys()) { // match platform
            const QJsonValue platform_value = game_object.value(platform_key);
            if (!platform_value.isObject()) {
                qDebug() << "Database Error - Invalid platform object:" << platform_key
                         << "for game ID:" << game_id;
                continue;
            }
            if (platform_key != current_os) {
                continue; // skip non-matching platform
            }
            const QJsonObject platform_obj = platform_value.toObject();

            // Create and populate the status structure
            QString normalized =
                NormalizeStatusString(platform_obj.value("status").toString("NoResult"));
            if (normalized.startsWith("Unknown")) {
                normalized = "NoResult";
            }
            Compat::Status status = m_status_data.at(normalized);

            QString isoDate = platform_obj.value("last_tested").toString();
            QDateTime dt = QDateTime::fromString(isoDate, Qt::ISODate);
            dt.setTimeZone(QTimeZone::utc());
            status.last_tested_date = dt.toString("yyyy/MM/dd");
            status.latest_version = platform_obj.value("version").toString();
            status.issue_number = platform_obj.value("issue_number").toString();

            // Add status to map
            m_compat_database.emplace(game_id.toStdString(), std::move(status));
        }
    }

    return true;
}
