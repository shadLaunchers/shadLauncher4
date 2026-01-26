// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QNetworkRequest>
#include <QTimer>
#include "downloader.h"
#include "gui_settings.h"
#include "progress_dialog.h"

Downloader::Downloader(std::shared_ptr<GUISettings> gui_settings, std::optional<GUISave> etag,
                       std::optional<GUISave> last_modified, QWidget* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)), m_parent(parent),
      m_gui_settings(std::move(gui_settings)), m_etag(std::move(etag)),
      m_last_modified(std::move(last_modified)) {}

Downloader::~Downloader() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_progress_dialog) {
        m_progress_dialog->close();
        m_progress_dialog = nullptr;
    }
}

void Downloader::DownloadJSONWithCache(const std::string& url, const QString& local_path,
                                       bool show_progress_dialog,
                                       const QString& progress_dialog_title, int delayMs) {
    m_localPath = local_path;
    m_data.clear();
    m_abort = false;
    m_closeDelayMs = delayMs;

    QNetworkRequest request(QUrl(QString::fromStdString(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    const bool hasLocalCache = QFile::exists(local_path);

    // --- Add caching headers if optional values are provided
    if (hasLocalCache) {
        if (m_etag && !m_etag->key.isEmpty()) {
            const QString etagValue = m_gui_settings->GetValue(*m_etag).toString();
            if (!etagValue.isEmpty())
                request.setRawHeader("If-None-Match", etagValue.toUtf8());
        }

        if (m_last_modified && !m_last_modified->key.isEmpty()) {
            const QString lastModifiedValue = m_gui_settings->GetValue(*m_last_modified).toString();
            if (!lastModifiedValue.isEmpty())
                request.setRawHeader("If-Modified-Since", lastModifiedValue.toUtf8());
        }
    } else {
        qDebug() << "Downloader: No local cache, forcing full download for" << local_path;
    }

    // --- Begin the download
    m_reply = m_manager->get(request);
    m_reply->setParent(this);

    // --- Connect download signals
    connect(m_reply, &QNetworkReply::readyRead, this, &Downloader::OnReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &Downloader::OnDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &Downloader::OnFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &Downloader::OnError);

    // --- Optional progress dialog
    if (show_progress_dialog) {
        const int maximum = 100;
        if (!m_progress_dialog) {
            m_progress_dialog = new ProgressDialog(progress_dialog_title, tr("Please wait..."),
                                                   tr("Abort"), 0, maximum, true, m_parent);
            m_progress_dialog->setAutoClose(true);
            m_progress_dialog->setAutoReset(false);
            m_progress_dialog->show();

            connect(m_progress_dialog, &QProgressDialog::canceled, this, [this]() {
                m_abort = true;
                if (m_reply)
                    m_reply->abort();
                m_progress_dialog = nullptr;
                Q_EMIT SignalDownloadCanceled();
            });

            connect(m_progress_dialog, &QProgressDialog::finished, this,
                    [this]() { m_progress_dialog = nullptr; });
        } else {
            m_progress_dialog->setWindowTitle(progress_dialog_title);
            m_progress_dialog->SetRange(0, maximum);
            m_progress_dialog->show();
        }
    }
}

void Downloader::OnReadyRead() {
    if (m_abort)
        return;
    m_data.append(m_reply->readAll());
    Q_EMIT SignalBufferUpdate(
        m_data.size(),
        static_cast<int>(m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()));
}

void Downloader::OnDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (m_abort)
        return;
    if (m_progress_dialog && bytesTotal > 0) {
        m_progress_dialog->SetRange(0, static_cast<int>(bytesTotal));
        m_progress_dialog->SetValue(static_cast<int>(bytesReceived));
        QApplication::processEvents();
    }
}

void Downloader::OnFinished() {
    if (m_abort) {
        if (m_reply) {
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        return;
    }

    if (!m_reply)
        return;

    const int status_code = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // --- Handle HTTP 304: Not Modified
    if (status_code == 304) {
        QFile file(m_localPath);
        if (file.open(QIODevice::ReadOnly)) {
            Q_EMIT SignalDownloadFinished(file.readAll());
            file.close();
        } else {
            Q_EMIT SignalDownloadError(tr("Local file missing after 304."));
        }
        if (m_progress_dialog)
            CloseProgressDialog();
        m_reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    // --- Handle HTTP errors
    if (status_code >= 400) {
        qWarning() << "HTTP error" << status_code << "on URL:" << m_reply->url();
        QFile::remove(m_localPath);
        Q_EMIT SignalDownloadError(QString("HTTP error %1").arg(status_code));
        if (m_progress_dialog)
            CloseProgressDialog();
        m_reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    // --- Handle network errors
    if (m_reply->error() != QNetworkReply::NoError) {
        QFile file(m_localPath);
        if (file.open(QIODevice::ReadOnly)) {
            Q_EMIT SignalDownloadFinished(file.readAll());
            file.close();
        } else {
            Q_EMIT SignalDownloadError(m_reply->errorString());
        }
        if (m_progress_dialog)
            CloseProgressDialog();
        m_reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    // --- HTTP 200 OK: Save response and update optional ETag / Last-Modified
    const QString new_etag = QString::fromUtf8(m_reply->rawHeader("ETag"));
    if (m_etag && !new_etag.isEmpty())
        m_gui_settings->SetValue(*m_etag, new_etag);

    const QString new_last_modified = QString::fromUtf8(m_reply->rawHeader("Last-Modified"));
    if (m_last_modified && !new_last_modified.isEmpty())
        m_gui_settings->SetValue(*m_last_modified, new_last_modified);

    QFile file(m_localPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_data);
        file.close();
    } else {
        Q_EMIT SignalDownloadError(tr("Cannot write output file."));
        if (m_progress_dialog)
            CloseProgressDialog();
        m_reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    if (m_progress_dialog)
        CloseProgressDialog(m_closeDelayMs);

    Q_EMIT SignalDownloadFinished(m_data);

    m_reply->deleteLater();
    m_reply = nullptr;
}

void Downloader::OnError(QNetworkReply::NetworkError) {
    if (m_abort)
        return;

    QString error = m_reply ? m_reply->errorString() : tr("Unknown network error");
    Q_EMIT SignalDownloadError(error);

    if (m_progress_dialog)
        CloseProgressDialog();
}

void Downloader::CloseProgressDialog(int delayMs) {
    if (!m_progress_dialog)
        return;

    if (delayMs > 0) {
        QTimer::singleShot(delayMs, [this]() {
            if (m_progress_dialog) {
                m_progress_dialog->accept();
                m_progress_dialog = nullptr;
            }
        });
    } else {
        m_progress_dialog->accept();
        m_progress_dialog = nullptr;
    }
}

ProgressDialog* Downloader::GetProgressDialog() const {
    return m_progress_dialog;
}