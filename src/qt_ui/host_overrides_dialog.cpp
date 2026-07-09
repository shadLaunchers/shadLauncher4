// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <set>

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include "common/path_util.h"
#include "host_overrides_dialog.h"

namespace {

constexpr int ColEnabled = 0;
constexpr int ColMatch = 1;
constexpr int ColTarget = 2;

// Validate a redirect *target* value against
//   host | host:port | http(s)://host | http(s)://host:port
QString ValidateTarget(const QString& raw) {
    QString value = raw.trimmed();
    if (value.isEmpty()) {
        return QObject::tr("Redirect target is empty.");
    }
    // Optional scheme.
    if (const int sep = value.indexOf("://"); sep != -1) {
        const QString scheme = value.left(sep).toLower();
        if (scheme != "http" && scheme != "https") {
            return QObject::tr("Target scheme must be http or https.");
        }
        value = value.mid(sep + 3);
    }
    if (value.isEmpty()) {
        return QObject::tr("Redirect target has no host.");
    }
    // Optional :port
    if (const int colon = value.indexOf(':'); colon != -1) {
        const QString host = value.left(colon);
        const QString port_str = value.mid(colon + 1);
        if (host.isEmpty()) {
            return QObject::tr("Redirect target has no host.");
        }
        bool ok = false;
        const int port = port_str.toInt(&ok);
        if (!ok || port < 1 || port > 65535) {
            return QObject::tr("Port must be a number between 1 and 65535.");
        }
    }
    return {};
}

// Validate a *match* key. Allows "*", "host", "host:port", "scheme://host:port".
QString ValidateMatch(const QString& raw) {
    const QString key = raw.trimmed();
    if (key.isEmpty()) {
        return QObject::tr("Match host is empty.");
    }
    if (key == "*") {
        return {};
    }
    QString rest = key;
    if (const int sep = rest.indexOf("://"); sep != -1) {
        const QString scheme = rest.left(sep).toLower();
        if (scheme != "http" && scheme != "https") {
            return QObject::tr("Match scheme must be http or https (or omit it).");
        }
        rest = rest.mid(sep + 3);
    }
    if (rest.isEmpty()) {
        return QObject::tr("Match has no host.");
    }
    if (const int colon = rest.indexOf(':'); colon != -1) {
        bool ok = false;
        const int port = rest.mid(colon + 1).toInt(&ok);
        if (!ok || port < 1 || port > 65535) {
            return QObject::tr("Match port must be a number between 1 and 65535.");
        }
    }
    return {};
}

struct OverrideEntry {
    QString match;
    QString target;
    bool enabled;
};

std::vector<OverrideEntry> ParseOverridesJson(const nlohmann::ordered_json& root) {
    std::vector<OverrideEntry> entries;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().is_string()) {
            continue;
        }
        QString key = QString::fromStdString(it.key());
        bool enabled = true;
        if (key.startsWith('_')) {
            enabled = false;
            key.remove(0, 1);
        }
        entries.push_back(OverrideEntry{
            std::move(key), QString::fromStdString(it.value().get<std::string>()), enabled});
    }
    return entries;
}

nlohmann::ordered_json BuildOverridesJson(const QTableWidget* table, QStringList& problems) {
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    std::set<QString> seen_keys;

    for (int r = 0; r < table->rowCount(); ++r) {
        const QTableWidgetItem* match_item = table->item(r, ColMatch);
        const QTableWidgetItem* target_item = table->item(r, ColTarget);
        const QTableWidgetItem* check_item = table->item(r, ColEnabled);

        const QString match = match_item ? match_item->text().trimmed() : QString();
        const QString target = target_item ? target_item->text().trimmed() : QString();
        const bool enabled = check_item && check_item->checkState() == Qt::Checked;

        if (match.isEmpty() && target.isEmpty()) {
            continue; // skip blank rows silently
        }

        if (const QString err = ValidateMatch(match); !err.isEmpty()) {
            problems << QObject::tr("Row %1: %2").arg(r + 1).arg(err);
            continue;
        }
        if (const QString err = ValidateTarget(target); !err.isEmpty()) {
            problems << QObject::tr("Row %1: %2").arg(r + 1).arg(err);
            continue;
        }

        // Disabled rows are persisted with a leading '_' so the loader skips them.
        const QString effective_key = enabled ? match : QStringLiteral("_") + match;
        if (seen_keys.count(effective_key)) {
            problems << QObject::tr("Row %1: duplicate entry for '%2'.").arg(r + 1).arg(match);
            continue;
        }
        seen_keys.insert(effective_key);
        root[effective_key.toStdString()] = target.toStdString();
    }

    return root;
}

} // namespace

std::filesystem::path HostOverridesDialog::FilePath() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "host_overrides.json";
}

HostOverridesDialog::HostOverridesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Network Host Overrides"));
    setObjectName("HostOverridesDialog");
    setModal(true);
    setMinimumSize(640, 420);

    auto* intro = new QLabel(this);
    intro->setObjectName("HostOverridesDialog_intro");
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setText(tr(
        "Redirect a game's network requests to a different server (for example a "
        "local private server). Each row maps a <b>match</b> to a <b>redirect target</b>. "
        "Matches are tried most-specific first:<br>"
        "&nbsp;&nbsp;<tt>host</tt> &middot; <tt>host:port</tt> &middot; "
        "<tt>scheme://host:port</tt> &middot; <tt>*</tt> (catch-all)<br>"
        "Targets look like <tt>host</tt>, <tt>host:port</tt>, or "
        "<tt>http(s)://host[:port]</tt>. Unchecked rows are saved but ignored by the emulator."));

    // Table
    m_table = new QTableWidget(this);
    m_table->setObjectName("HostOverridesDialog_table");
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {tr("Enabled"), tr("Match (host / endpoint)"), tr("Redirect to")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColMatch, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Editing buttons
    auto* add_btn = new QPushButton(tr("&Add"), this);
    auto* add_catchall_btn = new QPushButton(tr("Add &catch-all"), this);
    auto* remove_btn = new QPushButton(tr("&Remove"), this);
    auto* import_btn = new QPushButton(tr("&Import..."), this);
    auto* export_btn = new QPushButton(tr("&Export..."), this);
    add_btn->setAutoDefault(false);
    add_catchall_btn->setAutoDefault(false);
    remove_btn->setAutoDefault(false);
    import_btn->setAutoDefault(false);
    export_btn->setAutoDefault(false);

    auto* edit_row = new QHBoxLayout();
    edit_row->addWidget(add_btn);
    edit_row->addWidget(add_catchall_btn);
    edit_row->addWidget(remove_btn);
    edit_row->addWidget(import_btn);
    edit_row->addWidget(export_btn);
    edit_row->addStretch();

    connect(add_btn, &QPushButton::clicked, this, &HostOverridesDialog::OnAdd);
    connect(add_catchall_btn, &QPushButton::clicked, this, &HostOverridesDialog::OnAddCatchAll);
    connect(remove_btn, &QPushButton::clicked, this, &HostOverridesDialog::OnRemove);
    connect(import_btn, &QPushButton::clicked, this, &HostOverridesDialog::OnImport);
    connect(export_btn, &QPushButton::clicked, this, &HostOverridesDialog::OnExport);

    // Path label + open-folder
    m_path_label = new QLabel(this);
    m_path_label->setObjectName("HostOverridesDialog_path");
    m_path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    {
        QString p;
        Common::FS::PathToQString(p, FilePath());
        m_path_label->setText(tr("File: %1").arg(p));
    }
    {
        QFont f = m_path_label->font();
        f.setPointSizeF(f.pointSizeF() - 1.0);
        m_path_label->setFont(f);
    }

    // Dialog buttons
    auto* buttons = new QDialogButtonBox(this);
    auto* open_btn = buttons->addButton(tr("Open Folder"), QDialogButtonBox::ActionRole);
    auto* save_btn = buttons->addButton(QDialogButtonBox::Save);
    buttons->addButton(QDialogButtonBox::Close);
    save_btn->setDefault(true);

    connect(open_btn, &QPushButton::clicked, this, [this]() {
        QString dir;
        Common::FS::PathToQString(dir, FilePath().parent_path());
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (SaveToDisk()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addWidget(intro);
    root->addWidget(m_table, 1);
    root->addLayout(edit_row);
    root->addWidget(m_path_label);
    root->addWidget(buttons);
    setLayout(root);

    LoadFromDisk();
}

void HostOverridesDialog::AddRow(const QString& match, const QString& target, bool enabled) {
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* check = new QTableWidgetItem();
    check->setFlags((check->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    check->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    check->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColEnabled, check);

    m_table->setItem(row, ColMatch, new QTableWidgetItem(match));
    m_table->setItem(row, ColTarget, new QTableWidgetItem(target));
}

void HostOverridesDialog::OnAdd() {
    AddRow(QString(), QStringLiteral("http://localhost:8080"), true);
    m_table->editItem(m_table->item(m_table->rowCount() - 1, ColMatch));
}

void HostOverridesDialog::OnAddCatchAll() {
    // Don't add a second catch-all if one already exists.
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->item(r, ColMatch) && m_table->item(r, ColMatch)->text().trimmed() == "*") {
            QMessageBox::information(this, tr("Catch-all exists"),
                                     tr("A catch-all (*) row already exists."));
            return;
        }
    }
    AddRow(QStringLiteral("*"), QStringLiteral("http://localhost:8080"), true);
}

void HostOverridesDialog::OnRemove() {
    // Collect unique selected rows, delete from the bottom up.
    std::set<int, std::greater<int>> rows;
    for (const QModelIndex& idx : m_table->selectionModel()->selectedRows()) {
        rows.insert(idx.row());
    }
    if (rows.empty()) {
        if (const int cur = m_table->currentRow(); cur >= 0) {
            rows.insert(cur);
        }
    }
    for (const int r : rows) {
        m_table->removeRow(r);
    }
}

void HostOverridesDialog::OnImport() {
    const QString file_name = QFileDialog::getOpenFileName(
        this, tr("Import Host Overrides"), QString(), tr("JSON Files (*.json);;All Files (*)"));
    if (file_name.isEmpty()) {
        return;
    }

    std::ifstream in(file_name.toStdString());
    if (!in.is_open()) {
        QMessageBox::critical(this, tr("Import failed"), tr("Could not open the selected file."));
        return;
    }

    nlohmann::ordered_json root;
    try {
        in >> root;
    } catch (const std::exception&) {
        QMessageBox::warning(this, tr("Parse error"),
                             tr("The selected file could not be parsed as JSON."));
        return;
    }
    if (!root.is_object()) {
        QMessageBox::warning(
            this, tr("Format error"),
            tr("The selected file must be a JSON object mapping match keys to redirect "
               "targets, like host_overrides.json."));
        return;
    }

    const std::vector<OverrideEntry> entries = ParseOverridesJson(root);
    if (entries.empty()) {
        QMessageBox::information(this, tr("Nothing to import"),
                                 tr("The selected file doesn't contain any valid entries."));
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Import Host Overrides"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Found %1 entry/entries in the selected file.\n\n"
                   "Replace the current list entirely, or append these entries to it "
                   "(entries whose match already exists in the list will be skipped)?")
                    .arg(entries.size()));
    QPushButton* replace_btn = box.addButton(tr("Replace All"), QMessageBox::DestructiveRole);
    QPushButton* append_btn = box.addButton(tr("Append"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(append_btn);
    box.exec();

    const QAbstractButton* clicked = box.clickedButton();
    if (clicked == replace_btn) {
        m_table->setRowCount(0);
        for (const OverrideEntry& entry : entries) {
            AddRow(entry.match, entry.target, entry.enabled);
        }
        QMessageBox::information(
            this, tr("Import complete"),
            tr("Replaced the list with %1 imported entry/entries.").arg(entries.size()));
    } else if (clicked == append_btn) {
        // Match keys already present in the table, so we don't create duplicate rows.
        std::set<QString> existing;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (const QTableWidgetItem* match_item = m_table->item(r, ColMatch)) {
                existing.insert(match_item->text().trimmed());
            }
        }

        int added = 0;
        int skipped = 0;
        for (const OverrideEntry& entry : entries) {
            if (existing.count(entry.match)) {
                ++skipped;
                continue;
            }
            AddRow(entry.match, entry.target, entry.enabled);
            existing.insert(entry.match); // guard against duplicates within the imported file
            ++added;
        }

        if (skipped > 0) {
            QMessageBox::information(
                this, tr("Import complete"),
                tr("Added %1 new entry/entries. Skipped %2 already in the list.")
                    .arg(added)
                    .arg(skipped));
        } else {
            QMessageBox::information(this, tr("Import complete"),
                                     tr("Added %1 new entry/entries.").arg(added));
        }
    }
    // Cancel: leave the table untouched.
}

void HostOverridesDialog::OnExport() {
    QStringList problems;
    nlohmann::ordered_json root = BuildOverridesJson(m_table, problems);

    if (!problems.isEmpty()) {
        QMessageBox::warning(
            this, tr("Cannot export"),
            tr("Please fix the following before exporting:\n\n%1").arg(problems.join('\n')));
        return;
    }
    if (root.empty()) {
        QMessageBox::information(this, tr("Nothing to export"),
                                 tr("The list doesn't have any entries yet."));
        return;
    }

    QString default_path;
    Common::FS::PathToQString(default_path, FilePath());
    const QString file_name = QFileDialog::getSaveFileName(
        this, tr("Export Host Overrides"), default_path, tr("JSON Files (*.json);;All Files (*)"));
    if (file_name.isEmpty()) {
        return;
    }

    std::ofstream out(file_name.toStdString(), std::ios::trunc);
    if (!out.is_open()) {
        QMessageBox::critical(this, tr("Export failed"),
                              tr("Could not open the selected file for writing."));
        return;
    }
    out << root.dump(4) << '\n';
    if (out.fail()) {
        QMessageBox::critical(this, tr("Export failed"), tr("An error occurred while writing."));
        return;
    }

    QMessageBox::information(this, tr("Export complete"),
                             tr("Exported %1 entry/entries.").arg(root.size()));
}

void HostOverridesDialog::LoadFromDisk() {
    m_table->setRowCount(0);

    const std::filesystem::path path = FilePath();
    std::ifstream in(path);
    if (!in.is_open()) {
        return; // No file yet
    }

    nlohmann::ordered_json root;
    try {
        in >> root;
    } catch (const std::exception&) {
        QMessageBox::warning(
            this, tr("Parse error"),
            tr("The existing host_overrides.json could not be parsed. Starting with an "
               "empty list; saving will overwrite the file."));
        m_table->setRowCount(0);
        return;
    }
    if (!root.is_object()) {
        QMessageBox::warning(this, tr("Format error"),
                             tr("host_overrides.json must be a JSON object. Starting empty."));
        return;
    }

    for (const OverrideEntry& entry : ParseOverridesJson(root)) {
        AddRow(entry.match, entry.target, entry.enabled);
    }
}

bool HostOverridesDialog::SaveToDisk() {
    QStringList problems;
    nlohmann::ordered_json root = BuildOverridesJson(m_table, problems);

    if (!problems.isEmpty()) {
        QMessageBox::warning(
            this, tr("Cannot save"),
            tr("Please fix the following before saving:\n\n%1").arg(problems.join('\n')));
        return false;
    }

    const std::filesystem::path path = FilePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        QString p;
        Common::FS::PathToQString(p, path);
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not open the file for writing:\n%1").arg(p));
        return false;
    }
    out << root.dump(4) << '\n';
    if (out.fail()) {
        QMessageBox::critical(this, tr("Save failed"), tr("An error occurred while writing."));
        return false;
    }
    return true;
}
