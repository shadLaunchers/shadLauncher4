// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/path_util.h>
#include "downloader.h"
#include "game_compatibility.h"
#include "gui_settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrentRun>

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

GameCompatibility::~GameCompatibility() {
    if (m_parse_watcher) {
        m_parse_watcher->waitForFinished();
    }
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
    qDebug() << "Database download finished, parsing in background";
    StartParse(content, /*after_download=*/true);
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

        qDebug() << "Finished reading database from file, parsing in background:" << m_filepath;
        StartParse(content, /*after_download=*/false);

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

std::optional<std::map<std::string, Compat::Status>> GameCompatibility::ParseDatabase(
    const QByteArray& content) const {
    const QJsonObject json_data = QJsonDocument::fromJson(content).object();

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
        return std::nullopt;
    }

    std::map<std::string, Compat::Status> database;

    for (auto it = json_data.constBegin(); it != json_data.constEnd(); ++it) {
        const QString& game_id = it.key();
        const QJsonValue& game_value = it.value();
        if (!game_value.isObject()) {
            qDebug() << "Database Error - Unusable object:" << game_id;
            continue;
        }
        const QJsonObject game_object = game_value.toObject();
        for (auto platform_it = game_object.constBegin(); platform_it != game_object.constEnd();
             ++platform_it) {
            if (platform_it.key() != current_os) {
                continue; // skip non-matching platform
            }
            const QJsonValue& platform_value = platform_it.value();
            if (!platform_value.isObject()) {
                qDebug() << "Database Error - Invalid platform object:" << platform_it.key()
                         << "for game ID:" << game_id;
                continue;
            }
            const QJsonObject platform_obj = platform_value.toObject();

            // Create and populate the status structure
            QString normalized =
                NormalizeStatusString(platform_obj.value("status").toString("NoResult"));
            if (normalized.startsWith("Unknown")) {
                normalized = "NoResult";
            }
            const auto status_it = m_status_data.find(normalized);
            if (status_it == m_status_data.end()) {
                continue;
            }
            Compat::Status status = status_it->second;

            QString isoDate = platform_obj.value("last_tested").toString();
            QDateTime dt = QDateTime::fromString(isoDate, Qt::ISODate);
            dt.setTimeZone(QTimeZone::utc());
            status.last_tested_date = dt.toString("yyyy/MM/dd");
            status.latest_version = platform_obj.value("version").toString();
            status.issue_number = platform_obj.value("issue_number").toString();

            // Add status to map
            database.emplace(game_id.toStdString(), std::move(status));
        }
    }

    return database;
}

void GameCompatibility::StartParse(const QByteArray& content, bool after_download) {
    if (!m_parse_watcher) {
        m_parse_watcher =
            new QFutureWatcher<std::optional<std::map<std::string, Compat::Status>>>(this);
        connect(m_parse_watcher, &QFutureWatcherBase::finished, this,
                &GameCompatibility::OnParseFinished);
    }

    m_pending_after_download = after_download;
    m_pending_write_content = after_download ? content : QByteArray();

    m_parse_watcher->setFuture(
        QtConcurrent::run([this, content]() { return ParseDatabase(content); }));
}

void GameCompatibility::OnParseFinished() {
    const auto result = m_parse_watcher->result();
    const bool after_download = m_pending_after_download;

    if (!result.has_value()) {
        Q_EMIT DownloadError(tr("Error Downloading Compatibility Database"));
        return;
    }

    m_compat_database = std::move(*result);

    if (after_download) {
        // Only persist to disk once we know the download actually parsed
        // into something usable.
        QFile file(m_filepath);

        if (file.exists()) {
            qDebug() << "Database file found:" << m_filepath;
        }

        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Could not write database to file:" << m_filepath;
        } else {
            file.write(m_pending_write_content);
            file.close();
            qDebug() << "Wrote database to file:" << m_filepath;
        }
        m_pending_write_content.clear();
    }

    Q_EMIT DownloadFinished();
}
