// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <vector>

#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QTableWidget>
#include <QToolBar>

struct DlcEntry {
    QString name;         // TITLE from sce_sys/param.sfo, falls back to the folder name
    QString contentId;    // CONTENT_ID from sce_sys/param.sfo
    QString entitlement;  // Folder name (entitlement label)
    QString path;         // Absolute folder path
    QString ownerTitleId; // TitleID of the folder this entry was found under
    int serviceLabel = 0; // 0 = this game's own content, 1-7 = shared content label
    quint64 sizeBytes = 0;
    QPixmap icon;
};

class DlcViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit DlcViewerDialog(QWidget* parent, const QString& gameName, const QString& gameSerial,
                             const std::filesystem::path& gameRootPath,
                             const std::filesystem::path& gameUpdatePath,
                             const std::filesystem::path& addonInstallDir);
    ~DlcViewerDialog() override;

private:
    void setupUi();
    void reload();
    std::vector<std::pair<int, QString>> resolveSharedContentSources() const;
    std::vector<DlcEntry> scanTitleAddonDir(const std::filesystem::path& dir,
                                            const QString& ownerTitleId, int serviceLabel) const;
    std::vector<DlcEntry> scanAll() const;
    void populateTable(const std::vector<DlcEntry>& entries);
    void updateSummary();

private slots:
    void onOpenAddonFolder();
    void onOpenSelectedFolder();
    void onDeleteSelected();
    void onReload();

private:
    QString m_gameName;
    QString m_gameSerial;
    std::filesystem::path m_gameRootPath;
    std::filesystem::path m_gameUpdatePath;
    std::filesystem::path m_addonInstallDir;
    std::filesystem::path m_gameAddonDir; // m_addonInstallDir / m_gameSerial

    QToolBar* m_toolbar = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summaryLabel = nullptr;
    std::vector<DlcEntry> m_entries;
};
