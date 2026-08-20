// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs_util.h"
#include "common/path_util.h"
#include "common/string_util.h"
#include "core/file_format/psf.h"
#include "core/file_sys/game_backend.h"
#include "dlc_viewer_dialog.h"
#include "qt_utils.h"

namespace {
constexpr int ColIcon = 0;
constexpr int ColName = 1;
constexpr int ColContentId = 2;
constexpr int ColSize = 3;
constexpr int ColSource = 4;
constexpr int ColCount = 5;
constexpr int kMaxServiceLabel = 7; // SERVICE_ID_ADDCONT_ADD_1 .. _7 per the AppContent reference
} // namespace

DlcViewerDialog::DlcViewerDialog(QWidget* parent, const QString& gameName,
                                 const QString& gameSerial,
                                 const std::filesystem::path& gameRootPath,
                                 const std::filesystem::path& gameUpdatePath,
                                 const std::filesystem::path& addonInstallDir)
    : QDialog(parent), m_gameName(gameName), m_gameSerial(gameSerial), m_gameRootPath(gameRootPath),
      m_gameUpdatePath(gameUpdatePath), m_addonInstallDir(addonInstallDir) {
    setWindowTitle(tr("DLC Viewer") + " - " + gameName);
    resize(920, 480);

    if (!m_addonInstallDir.empty()) {
        m_gameAddonDir = m_addonInstallDir / gameSerial.toStdString();
    }

    setupUi();
    reload();
}

DlcViewerDialog::~DlcViewerDialog() = default;

void DlcViewerDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);

    m_toolbar = new QToolBar(this);
    QAction* reloadAct = m_toolbar->addAction(tr("Reload"));
    QAction* openAddonFolderAct = m_toolbar->addAction(tr("Open Addon Folder"));
    QAction* openSelectedAct = m_toolbar->addAction(tr("Open DLC Folder"));
    QAction* deleteAct = m_toolbar->addAction(tr("Delete DLC"));
    mainLayout->addWidget(m_toolbar);

    connect(reloadAct, &QAction::triggered, this, &DlcViewerDialog::onReload);
    connect(openAddonFolderAct, &QAction::triggered, this, &DlcViewerDialog::onOpenAddonFolder);
    connect(openSelectedAct, &QAction::triggered, this, &DlcViewerDialog::onOpenSelectedFolder);
    connect(deleteAct, &QAction::triggered, this, &DlcViewerDialog::onDeleteSelected);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels(
        {tr(""), tr("Name"), tr("Content ID"), tr("Size"), tr("Source")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setIconSize(QSize(48, 48));
    m_table->horizontalHeader()->setSectionResizeMode(ColIcon, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColContentId, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColSize, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::ResizeToContents);
    mainLayout->addWidget(m_table);

    m_summaryLabel = new QLabel(this);
    mainLayout->addWidget(m_summaryLabel);
}

// Reads the game's own param.sfo and returns (serviceLabel, titleId) for each
// populated SERVICE_ID_ADDCONT_ADD_1..7 key. Service IDs are of the form
// "UP4108-CUSA01665_00" - the title id is the segment between '-' and '_'.
std::vector<std::pair<int, QString>> DlcViewerDialog::resolveSharedContentSources() const {
    std::vector<std::pair<int, QString>> sources;

    const std::filesystem::path base_path =
        !m_gameUpdatePath.empty() ? m_gameUpdatePath : m_gameRootPath;
    if (base_path.empty()) {
        return sources;
    }

    const auto sfo_buffer = Core::FileSys::ReadGameFile(base_path, "sce_sys/param.sfo");
    if (!sfo_buffer.has_value()) {
        return sources;
    }

    PSF psf;
    if (!psf.Open(*sfo_buffer)) {
        return sources;
    }

    for (int label = 1; label <= kMaxServiceLabel; ++label) {
        const std::string key = "SERVICE_ID_ADDCONT_ADD_" + std::to_string(label);
        const auto service_id = psf.GetString(key);
        if (!service_id.has_value() || service_id->empty()) {
            continue;
        }

        // "UP4108-CUSA01665_00" -> ["UP4108", "CUSA01665_00"] -> "CUSA01665"
        const auto dash_parts = Common::SplitString(std::string{*service_id}, '-');
        if (dash_parts.size() < 2) {
            continue;
        }
        const auto title_parts = Common::SplitString(dash_parts[1], '_');
        if (title_parts.empty() || title_parts[0].empty()) {
            continue;
        }

        sources.emplace_back(label, QString::fromStdString(title_parts[0]));
    }

    return sources;
}

std::vector<DlcEntry> DlcViewerDialog::scanTitleAddonDir(const std::filesystem::path& dir,
                                                         const QString& ownerTitleId,
                                                         int serviceLabel) const {
    std::vector<DlcEntry> entries;

    std::error_code ec;
    if (dir.empty() || !std::filesystem::exists(dir, ec) || ec) {
        return entries;
    }

    for (const auto& dirEntry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !dirEntry.is_directory()) {
            continue;
        }

        DlcEntry entry;
        entry.entitlement = QString::fromStdString(dirEntry.path().filename().string());
        entry.name = entry.entitlement;
        entry.ownerTitleId = ownerTitleId;
        entry.serviceLabel = serviceLabel;
        QString pathQt;
        Common::FS::PathToQString(pathQt, dirEntry.path());
        entry.path = pathQt;

        const auto param_sfo_path = dirEntry.path() / "sce_sys" / "param.sfo";
        PSF psf;
        if (std::filesystem::exists(param_sfo_path, ec) && psf.Open(param_sfo_path)) {
            if (const auto title = psf.GetString("TITLE"); title.has_value() && !title->empty()) {
                entry.name = QString::fromStdString(std::string{*title});
            }
            if (const auto content_id = psf.GetString("CONTENT_ID"); content_id.has_value()) {
                entry.contentId = QString::fromStdString(std::string{*content_id});
            }
        }

        const auto icon_path = dirEntry.path() / "sce_sys" / "icon0.png";
        QString iconPathQt;
        Common::FS::PathToQString(iconPathQt, icon_path);
        if (QFileInfo::exists(iconPathQt)) {
            entry.icon = QPixmap(iconPathQt);
        }

        entry.sizeBytes = FS::Utils::GetDirSize(dirEntry.path().string(), 1, nullptr);

        entries.push_back(entry);
    }

    return entries;
}

std::vector<DlcEntry> DlcViewerDialog::scanAll() const {
    std::vector<DlcEntry> entries = scanTitleAddonDir(m_gameAddonDir, m_gameSerial, 0);

    // Shared additional content (SERVICE_ID_ADDCONT_ADD_1..7)
    for (const auto& [label, titleId] : resolveSharedContentSources()) {
        if (m_addonInstallDir.empty() || titleId.isEmpty()) {
            continue;
        }
        const std::filesystem::path shared_dir = m_addonInstallDir / titleId.toStdString();
        auto shared_entries = scanTitleAddonDir(shared_dir, titleId, label);
        if (shared_entries.empty()) {
            continue;
        }
        entries.insert(entries.end(), shared_entries.begin(), shared_entries.end());
    }

    std::sort(entries.begin(), entries.end(),
              [](const DlcEntry& a, const DlcEntry& b) { return a.name < b.name; });

    return entries;
}

void DlcViewerDialog::populateTable(const std::vector<DlcEntry>& entries) {
    m_table->setRowCount(static_cast<int>(entries.size()));

    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const DlcEntry& entry = entries[row];

        auto* iconItem = new QTableWidgetItem();
        if (!entry.icon.isNull()) {
            iconItem->setIcon(
                QIcon(entry.icon.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
        iconItem->setData(Qt::UserRole, entry.path);
        m_table->setItem(row, ColIcon, iconItem);

        auto* nameItem = new QTableWidgetItem(entry.name);
        nameItem->setToolTip(entry.path);
        m_table->setItem(row, ColName, nameItem);

        m_table->setItem(
            row, ColContentId,
            new QTableWidgetItem(entry.contentId.isEmpty() ? entry.entitlement : entry.contentId));
        m_table->setItem(row, ColSize,
                         new QTableWidgetItem(GUI::Utils::FormatByteSize(entry.sizeBytes)));

        const QString sourceText =
            entry.serviceLabel == 0
                ? tr("This game")
                : tr("Shared (label %1, %2)").arg(entry.serviceLabel).arg(entry.ownerTitleId);
        auto* sourceItem = new QTableWidgetItem(sourceText);
        sourceItem->setToolTip(entry.ownerTitleId);
        m_table->setItem(row, ColSource, sourceItem);

        m_table->setRowHeight(row, 56);
    }
}

void DlcViewerDialog::updateSummary() {
    quint64 totalBytes = 0;
    for (const auto& entry : m_entries) {
        totalBytes += entry.sizeBytes;
    }

    QString dirQt;
    if (!m_gameAddonDir.empty()) {
        Common::FS::PathToQString(dirQt, m_gameAddonDir);
    }

    m_summaryLabel->setText(tr("%1 DLC(s) installed, %2 total")
                                .arg(static_cast<int>(m_entries.size()))
                                .arg(GUI::Utils::FormatByteSize(totalBytes)) +
                            (dirQt.isEmpty() ? QString() : "\n" + dirQt));
}

void DlcViewerDialog::reload() {
    if (m_addonInstallDir.empty()) {
        m_entries.clear();
        m_table->setRowCount(0);
        m_summaryLabel->setText(
            tr("No addon (DLC) install directory is configured. Set one in Settings."));
        return;
    }

    m_entries = scanAll();
    populateTable(m_entries);
    updateSummary();

    if (m_entries.empty()) {
        m_summaryLabel->setText(tr("No installed DLC found for %1.").arg(m_gameName));
    }
}

void DlcViewerDialog::onReload() {
    reload();
}

void DlcViewerDialog::onOpenAddonFolder() {
    if (m_addonInstallDir.empty()) {
        QMessageBox::information(
            this, tr("DLC Viewer"),
            tr("No addon (DLC) install directory is configured. Set one in Settings."));
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(m_addonInstallDir, ec)) {
        std::filesystem::create_directories(m_addonInstallDir, ec);
    }

    QString dirQt;
    Common::FS::PathToQString(dirQt, m_addonInstallDir);
    if (!QFileInfo::exists(dirQt)) {
        QMessageBox::critical(this, tr("DLC Viewer"),
                              tr("Could not open or create the addon folder:\n%1").arg(dirQt));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirQt));
}

void DlcViewerDialog::onOpenSelectedFolder() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= static_cast<int>(m_entries.size())) {
        QMessageBox::information(this, tr("DLC Viewer"), tr("Select a DLC first."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_entries[row].path));
}

void DlcViewerDialog::onDeleteSelected() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= static_cast<int>(m_entries.size())) {
        QMessageBox::information(this, tr("DLC Viewer"), tr("Select a DLC first."));
        return;
    }

    const DlcEntry& entry = m_entries[row];
    const auto reply = QMessageBox::question(
        this, tr("Delete DLC"),
        tr("Are you sure you want to delete this DLC?\n\n%1\n%2").arg(entry.name, entry.path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    std::error_code ec;
    std::filesystem::remove_all(Common::FS::PathFromQString(entry.path), ec);
    if (ec) {
        QMessageBox::critical(this, tr("DLC Viewer"),
                              tr("Failed to delete DLC folder:\n%1").arg(entry.path));
    }

    reload();
}
