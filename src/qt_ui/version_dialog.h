// SPDX-FileCopyrightText: Copyright 2025 shadLauncher4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QTextBrowser>
#include <QTreeWidget>

#include <QNetworkReply>
#include "downloader.h"
#include "gui_settings.h"

namespace Ui {
class VersionDialog;
}

class VersionDialog : public QDialog {
    Q_OBJECT
signals:
    void WindowResized(QResizeEvent* event);

public:
    explicit VersionDialog(std::shared_ptr<GUISettings> m_gui_settings, QWidget* parent = nullptr);
    ~VersionDialog();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void checkUpdatePre(const bool showMessage);
    void DownloadListVersion();
    void InstallSelectedVersion();

private Q_SLOTS:
    void HandleResize(QResizeEvent* event);

private:
    Ui::VersionDialog* ui;
    std::shared_ptr<GUISettings> m_gui_settings;
    QNetworkAccessManager* networkManager;

    void LoadInstalledList();
    QStringList LoadDownloadCache();
    void PopulateDownloadTree();
    void showPreReleaseUpdateDialog(const QString& localHash, const QString& latestHash,
                                    const QString& latestTag);
    void requestChangelog(const QString& localHash, const QString& latestHash,
                          const QString& latestTag, QTextBrowser* outputView);
    void installPreReleaseByTag(const QString& tagName);
    void showDownloadDialog(const QString& tagName, const QString& downloadUrl);
    Downloader* m_downloader = nullptr;

protected:
    void resizeEvent(QResizeEvent* event) override;
};