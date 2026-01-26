// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <memory>
#include <optional>
#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include "gui_save.h"

class GUISettings;
class ProgressDialog;

class Downloader : public QObject {
    Q_OBJECT
public:
    Downloader(std::shared_ptr<GUISettings> gui_settings,
               std::optional<GUISave> etag = std::nullopt,
               std::optional<GUISave> last_modified = std::nullopt, QWidget* parent = nullptr);
    ~Downloader();

    // Fixed declaration to match cpp
    void DownloadJSONWithCache(const std::string& url, const QString& local_path,
                               bool show_progress_dialog = true,
                               const QString& progress_dialog_title = QString(), int delayMs = 0);

    void CloseProgressDialog(int delayMs = 0);
    ProgressDialog* GetProgressDialog() const;

signals:
    void SignalDownloadFinished(const QByteArray& data);
    void SignalDownloadError(const QString& error);
    void SignalDownloadCanceled();
    void SignalBufferUpdate(qint64 received, int total);

private slots:
    void OnReadyRead();
    void OnDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void OnFinished();
    void OnError(QNetworkReply::NetworkError code);

private:
    QString m_localPath;
    std::shared_ptr<GUISettings> m_gui_settings;
    QByteArray m_data;
    bool m_abort = false;
    bool m_keep_progress_dialog_open = false;
    int m_closeDelayMs = 0;

    ProgressDialog* m_progress_dialog = nullptr;
    QNetworkReply* m_reply = nullptr;
    QNetworkAccessManager* m_manager = nullptr;
    QWidget* m_parent = nullptr;
    std::optional<GUISave> m_etag;
    std::optional<GUISave> m_last_modified;
};
