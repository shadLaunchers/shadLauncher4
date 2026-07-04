// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPixmap>
#include <QSplitter>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include "core/file_format/psf.h"
#include "sfo_key_map.h"
#include "sfo_viewer_dialog.h"
#include "table_item_delegate.h"

SFOViewerDialog::SFOViewerDialog(QWidget* parent, const QString& sfoPath)
    : QDialog(parent), m_sfoPath(sfoPath), m_sfoInfo(sfoPath) {
    setWindowTitle(tr("PS4 SFO Viewer"));
    resize(1000, 520);
    setupUi();
    loadSFO(m_sfoPath);
}

SFOViewerDialog::~SFOViewerDialog() = default;

void SFOViewerDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* toolbar = new QToolBar(this);

    QAction* reloadAct = toolbar->addAction(tr("Reload"));
    QAction* exportAct = toolbar->addAction(tr("Export"));
    QAction* openFolderAct = toolbar->addAction(tr("Open Folder"));
    QAction* copyAct = toolbar->addAction(tr("Copy Value"));

    mainLayout->addWidget(toolbar);

    connect(reloadAct, &QAction::triggered, this, &SFOViewerDialog::onReload);
    connect(exportAct, &QAction::triggered, this, &SFOViewerDialog::onExport);
    connect(openFolderAct, &QAction::triggered, this, &SFOViewerDialog::onOpenFolder);
    connect(copyAct, &QAction::triggered, this, &SFOViewerDialog::onCopyValue);

    auto* splitter = new QSplitter(this);

    // Left panel
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(8, 8, 8, 8);

    m_iconLabel = new QLabel(leftWidget);
    m_iconLabel->setFixedSize(128, 128);
    m_iconLabel->setFrameShape(QFrame::Box);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setScaledContents(true);
    leftLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(tr("Title: -"), leftWidget);
    leftLayout->addWidget(m_titleLabel);
    m_titleIdLabel = new QLabel(tr("Title ID: -"), leftWidget);
    leftLayout->addWidget(m_titleIdLabel);
    m_versionLabel = new QLabel(tr("Version: -"), leftWidget);
    leftLayout->addWidget(m_versionLabel);

    m_localizedGroup = new QGroupBox(tr("Localized Titles"), leftWidget);
    auto* locLayout = new QVBoxLayout(m_localizedGroup);
    m_localizedList = new QListWidget(m_localizedGroup);
    locLayout->addWidget(m_localizedList);
    leftLayout->addWidget(m_localizedGroup);

    leftLayout->addStretch();

    // Right panel
    auto* rightWidget = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(8, 8, 8, 8);

    auto* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(rightWidget);
    m_searchEdit->setPlaceholderText(tr("Search..."));
    searchLayout->addWidget(m_searchEdit);

    m_regexCheck = new QCheckBox(tr("Regex"), rightWidget);
    searchLayout->addWidget(m_regexCheck);
    rightLayout->addLayout(searchLayout);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &SFOViewerDialog::onSearchTextChanged);
    connect(m_regexCheck, &QCheckBox::toggled, this, &SFOViewerDialog::onRegexToggled);

    m_tableView = new QTableView(rightWidget);
    m_model = new SFOModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_tableView->setModel(m_proxy);
    m_tableView->setSortingEnabled(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setItemDelegate(new TableItemDelegate(this, false));
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    QFontMetrics fm(m_tableView->font());
    m_tableView->verticalHeader()->setMinimumSectionSize(fm.height() + 8);

    rightLayout->addWidget(m_tableView);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);

    auto* h = m_tableView->horizontalHeader();
    h->setStretchLastSection(false);

    h->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Key
    h->setSectionResizeMode(1, QHeaderView::Stretch);          // Value
    h->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Type
}

bool SFOViewerDialog::loadSFO(const QString& path) {
    QApplication::setOverrideCursor(Qt::WaitCursor);

    PSF psf;
    if (!psf.Open(path.toStdWString())) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("PSF Error"), tr("Failed to load PSF file:\n%1").arg(path));
        return false;
    }

    std::vector<SFOEntry> entries;
    for (const auto& e : psf.GetEntries()) {
        SFOEntry se;
        se.key = QString::fromStdString(e.key);

        // Cached lookups
        if (e.param_fmt == PSFEntryFmt::Integer) {
            auto val = psf.GetInteger(e.key);
            se.value = val.has_value() ? QString::number(*val) : "<invalid>";
        } else if (e.param_fmt == PSFEntryFmt::Text) {
            auto val = psf.GetString(e.key);
            se.value =
                val.has_value() ? QString::fromUtf8(val->data(), int(val->size())) : "<invalid>";
        } else if (e.param_fmt == PSFEntryFmt::Binary) {
            auto val = psf.GetBinary(e.key);
            se.value = val.has_value() ? QString("(%1 bytes)").arg(val->size()) : "<invalid>";
        }

        se.type =
            SFOKeyDecoders::typeToString(e.param_fmt == PSFEntryFmt::Integer ? SFOValueType::Integer
                                         : e.param_fmt == PSFEntryFmt::Text  ? SFOValueType::String
                                                                            : SFOValueType::Binary);

        if (sfoKeyMap().contains(se.key)) {
            se.displayName = sfoKeyMap()[se.key].displayName;
            se.value = sfoKeyMap()[se.key].decodeFunc(se.value);
        } else {
            se.displayName = se.key;
        }

        if (se.key == "TITLE")
            m_titleLabel->setText(tr("Title: %1").arg(se.value));
        if (se.key == "TITLE_ID")
            m_titleIdLabel->setText(tr("Title ID: %1").arg(se.value));
        if (se.key == "APP_VER")
            m_versionLabel->setText(tr("Version: %1").arg(se.value));

        entries.push_back(se);
    }

    m_model->setEntries(entries);

    // Localized titles
    m_localizedList->clear();
    auto locales = extractLocalizedTitles(entries);

    // Add [Default] first
    if (locales.contains("Default")) {
        m_localizedList->addItem(QString("[Default] %1").arg(locales["Default"]));
        locales.remove("Default");
    }

    // Sort numeric keys properly
    QStringList keys = locales.keys();
    for (const QString& key : keys) {
        m_localizedList->addItem(
            QString("[%1] %2").arg(languageNameFromOrbisKey(key), locales[key]));
    }

    // Load icon
    QString iconPath;
    for (const QString& ext : {"png", "jpg"}) {
        QString path = m_sfoInfo.absolutePath() + "/icon0." + ext;
        if (QFile::exists(path)) {
            iconPath = path;
            break;
        }
    }

    QPixmap pix(iconPath);
    if (!pix.isNull())
        m_iconLabel->setPixmap(pix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        m_iconLabel->setText(tr("No Icon"));

    QApplication::restoreOverrideCursor();
    return true;
}

QMap<QString, QString> SFOViewerDialog::extractLocalizedTitles(
    const std::vector<SFOEntry>& entries) {
    QMap<QString, QString> locales;
    for (const auto& e : entries) {
        if (e.key == "TITLE") {
            locales["Default"] = e.value; // main title as [Default]
        } else if (e.key.startsWith("TITLE_") && e.key != "TITLE_ID") {
            locales[e.key.mid(6)] = e.value; // strip "TITLE_" prefix
        }
    }
    return locales;
}

void SFOViewerDialog::onReload() {
    loadSFO(m_sfoPath);
}

void SFOViewerDialog::onExport() {
    QString fileName =
        QFileDialog::getSaveFileName(this, tr("Export"), m_titleIdLabel->text().section(": ", 1, 1),
                                     tr("JSON (*.json);;CSV (*.csv);;HTML (*.html)"));
    if (fileName.isEmpty())
        return;

    if (fileName.endsWith(".csv")) {
        QFile f(fileName);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream out(&f);
        out << "Key,Value,Type\n";
        for (const auto& e : m_model->entries()) {
            // Use raw value (comma-separated) for CSV
            QString csvValue = e.value;
            // Escape quotes for CSV
            csvValue.replace("\"", "\"\"");
            out << "\"" << e.key << "\",\"" << csvValue << "\",\"" << e.type << "\"\n";
        }
    } else if (fileName.endsWith(".html")) {
        QFile f(fileName);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream out(&f);

        out << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
            << "<title>" << m_titleIdLabel->text().section(": ", 1, 1) << "</title>\n"
            << "<style>"
               "body { font-family: Arial, sans-serif; } "
               "table { border-collapse: collapse; margin-bottom: 20px; } "
               "th, td { border: 1px solid #ccc; padding: 4px; } "
               "th { background-color: #eee; }"
               ".attribute-value { white-space: pre-line; }" // Preserve line breaks
               "</style>\n</head>\n<body>\n";

        // Main title
        out << "<h1>" << m_titleLabel->text() << "</h1>\n";

        // Localized titles
        if (m_localizedList->count() != 0) {
            out << "<h2>Localized Titles</h2>\n<table>\n<tr><th>Language</th><th>Title</th></tr>\n";
            for (int i = 0; i < m_localizedList->count(); ++i) {
                QString text = m_localizedList->item(i)->text();
                QString lang = text.section(']', 0, 0).remove('[').trimmed();
                QString title = text.section(']', 1).trimmed();
                out << "<tr>"
                       "<td>"
                    << lang.toHtmlEscaped()
                    << "</td>"
                       "<td>"
                    << title.toHtmlEscaped() << "</td>"
                    << "</tr>\n";
            }
            out << "</table>\n";
        }

        // Main SFO entries
        out << "<h2>SFO Entries</h2>\n<table>\n<tr><th>Key</th><th>Value</th><th>Type</th></tr>\n";
        for (const auto& e : m_model->entries()) {
            QString displayValue = e.value;

            // Format attribute fields for HTML
            if ((e.key == "ATTRIBUTE" || e.key == "ATTRIBUTE2") && displayValue.contains(", ")) {
                // Replace commas with <br> for HTML line breaks
                displayValue.replace(", ", ",<br>");
                out << "<tr>"
                       "<td>"
                    << e.key.toHtmlEscaped()
                    << "</td>"
                       "<td class=\"attribute-value\">"
                    << displayValue // Already has <br> tags
                    << "</td>"
                       "<td>"
                    << e.type.toHtmlEscaped() << "</td>"
                    << "</tr>\n";
            } else {
                out << "<tr>"
                       "<td>"
                    << e.key.toHtmlEscaped()
                    << "</td>"
                       "<td>"
                    << displayValue.toHtmlEscaped()
                    << "</td>"
                       "<td>"
                    << e.type.toHtmlEscaped() << "</td>"
                    << "</tr>\n";
            }
        }
        out << "</table>\n";

        out << "</body>\n</html>\n";
    } else {
        // JSON export
        QJsonObject obj;
        for (const auto& e : m_model->entries()) {
            // For attributes, create an array instead of comma-separated string
            if ((e.key == "ATTRIBUTE" || e.key == "ATTRIBUTE2") && e.value.contains(", ")) {
                QStringList items = e.value.split(", ");
                QJsonArray array;
                for (const QString& item : items) {
                    array.append(item.trimmed());
                }
                obj[e.key] = array;
            } else {
                obj[e.key] = e.value;
            }
        }
        QFile f(fileName);
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

void SFOViewerDialog::onOpenFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_sfoInfo.absolutePath()));
}

void SFOViewerDialog::onCopyValue() {
    auto idx = m_tableView->currentIndex();
    if (!idx.isValid())
        return;
    auto src = m_proxy->mapToSource(idx);
    QApplication::clipboard()->setText(m_model->entries()[src.row()].value);
}

// ---------------- Search ----------------
void SFOViewerDialog::onSearchTextChanged(const QString& text) {
    applyFilter(text);
}

void SFOViewerDialog::onRegexToggled(bool) {
    applyFilter(m_searchEdit->text());
}

void SFOViewerDialog::applyFilter(const QString& text) {
    QRegularExpression regex;

    if (text.isEmpty()) {
        // Clear filter unambiguously
        m_proxy->setFilterRegularExpression(QRegularExpression());
        return;
    }

    if (m_regexCheck->isChecked()) {
        regex = QRegularExpression(text, QRegularExpression::CaseInsensitiveOption);
        // Fallback if regex is invalid
        if (!regex.isValid())
            regex.setPattern(QRegularExpression::escape(text));
    } else {
        regex.setPattern(QRegularExpression::escape(text));
        regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }

    m_proxy->setFilterRegularExpression(regex);
}
