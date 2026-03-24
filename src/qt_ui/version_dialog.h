// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
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
    void CopySelectedVersionToAppDir();

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
    bool CopyExecutableToAppDir(const QString& sourceExe, QWidget* parent);
    Downloader* m_downloader = nullptr;

protected:
    void resizeEvent(QResizeEvent* event) override;
};