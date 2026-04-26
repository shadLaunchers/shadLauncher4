// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "npbind_dialog.h"

#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

NpBindDialog::NpBindDialog(QWidget* parent, const QString& filePath)
    : QDialog(parent), m_filePath(filePath) {
    setWindowTitle(tr("npbind.dat Viewer"));
    resize(900, 600);

    if (!m_npfile.Load(filePath.toStdString())) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to parse npbind.dat"));
        reject();
        return;
    }

    auto* mainLayout = new QVBoxLayout(this);
    auto* topLayout = new QHBoxLayout();
    mainLayout->addLayout(topLayout);

    // Left: bodies list
    m_listBodies = new QListWidget(this);
    m_listBodies->setMinimumWidth(260);
    topLayout->addWidget(m_listBodies);

    // Right: details
    auto* detailWidget = new QWidget(this);
    auto* form = new QFormLayout(detailWidget);

    m_lblType = new QLabel(this);
    m_lblSize = new QLabel(this);
    m_lblNPCommID = new QLabel(this);
    m_lblTrophy = new QLabel(this);
    m_hexView = new QTextEdit(this);
    m_hexView->setReadOnly(true);
    m_hexView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_hexView->setMinimumHeight(300);
    m_hexView->setMouseTracking(true);
    m_hexView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_hexView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_hexView->setLineWrapMode(QTextEdit::NoWrap);

    m_lblNPCommID->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lblTrophy->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow(tr("Entry Type:"), m_lblType);
    form->addRow(tr("Entry Size:"), m_lblSize);
    form->addRow(tr("NPCommID:"), m_lblNPCommID);
    form->addRow(tr("Trophy ID:"), m_lblTrophy);
    form->addRow(tr("Hex Data:"), m_hexView);

    topLayout->addWidget(detailWidget, 1);

    // Region filter checkboxes
    auto* filterLayout = new QHBoxLayout();
    m_chkNPComm = new QCheckBox(tr("NPCommID"), this);
    m_chkNPComm->setChecked(true);
    m_chkTrophy = new QCheckBox(tr("Trophy"), this);
    m_chkTrophy->setChecked(true);
    m_chkUnk1 = new QCheckBox(tr("Unknown 1"), this);
    m_chkUnk1->setChecked(true);
    m_chkUnk2 = new QCheckBox(tr("Unknown 2"), this);
    m_chkUnk2->setChecked(true);

    filterLayout->addWidget(new QLabel(tr("Show regions:"), this));
    filterLayout->addWidget(m_chkNPComm);
    filterLayout->addWidget(m_chkTrophy);
    filterLayout->addWidget(m_chkUnk1);
    filterLayout->addWidget(m_chkUnk2);
    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Bottom buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_btnExport = new QPushButton(tr("Export JSON"), this);
    buttons->addButton(m_btnExport, QDialogButtonBox::ActionRole);
    mainLayout->addWidget(buttons);

    populateBodiesList();

    // Connections
    connect(m_listBodies, &QListWidget::currentRowChanged, this, &NpBindDialog::onBodySelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &NpBindDialog::reject);
    connect(m_btnExport, &QPushButton::clicked, this, &NpBindDialog::onExportJson);

    auto checkboxLambda = [this]() { QTimer::singleShot(0, this, &NpBindDialog::updateHexView); };
    connect(m_chkNPComm, &QCheckBox::checkStateChanged, checkboxLambda);
    connect(m_chkTrophy, &QCheckBox::checkStateChanged, checkboxLambda);
    connect(m_chkUnk1, &QCheckBox::checkStateChanged, checkboxLambda);
    connect(m_chkUnk2, &QCheckBox::checkStateChanged, checkboxLambda);

    connect(m_hexView, &QTextEdit::cursorPositionChanged, this, [this] {
        m_hexView->setToolTip(regionName(regionAt(m_hexView->textCursor().position())));
    });

    m_hexView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hexView, &QWidget::customContextMenuRequested, this,
            &NpBindDialog::onHexContextMenuRequested);

    if (m_listBodies->count() > 0)
        m_listBodies->setCurrentRow(0);
}

void NpBindDialog::populateBodiesList() {
    m_listBodies->clear();
    for (size_t i = 0; i < m_npfile.Bodies().size(); ++i)
        m_listBodies->addItem(tr("Body #%1").arg((int)i));
}

QString NpBindDialog::regionName(HexRegion r) const {
    switch (r) {
    case NP_COMM:
        return "NPCommID";
    case TROPHY:
        return "Trophy";
    case UNK1:
        return "Unknown 1";
    case UNK2:
        return "Unknown 2";
    }
    return "";
}

bool NpBindDialog::isRegionVisible(HexRegion r) const {
    switch (r) {
    case NP_COMM:
        return m_chkNPComm->isChecked();
    case TROPHY:
        return m_chkTrophy->isChecked();
    case UNK1:
        return m_chkUnk1->isChecked();
    case UNK2:
        return m_chkUnk2->isChecked();
    }
    return true;
}

QColor NpBindDialog::colorForRegion(HexRegion r) const {
    if (!isRegionVisible(r))
        return Qt::lightGray;
    switch (r) {
    case NP_COMM:
        return QColor("#00AAFF");
    case TROPHY:
        return QColor("#FFAA00");
    case UNK1:
        return QColor("#AA00FF");
    case UNK2:
        return QColor("#00AA00");
    }
    return Qt::black;
}

HexRegion NpBindDialog::regionAt(int pos) const {
    if (pos < 0 || pos >= (int)m_charToRegion.size())
        return NP_COMM;
    return m_charToRegion[pos];
}

QString NpBindDialog::formatHexWithRegions(const std::vector<uint8_t>& data,
                                           const std::vector<HexSegment>& regions) {
    QString out;
    m_charToRegion.clear();
    size_t dataSize = data.size();

    for (size_t i = 0; i < dataSize; i += 16) {
        QString line;
        line += QString("%1: ").arg((int)i, 8, 16, QChar('0')).toUpper();
        for (int k = 0; k < line.size(); ++k)
            m_charToRegion.push_back(NP_COMM);

        for (size_t j = 0; j < 16; ++j) {
            size_t idx = i + j;
            if (idx >= dataSize) {
                line += "   ";
                m_charToRegion.push_back(NP_COMM);
                continue;
            }

            HexRegion region = NP_COMM;
            for (const auto& seg : regions)
                if (idx >= seg.offset && idx < seg.offset + seg.size)
                    region = seg.type;

            line += QString("<span style=\"color:%1\" title=\"%2\">%3</span> ")
                        .arg(colorForRegion(region).name())
                        .arg(regionName(region))
                        .arg(data[idx], 2, 16, QChar('0'))
                        .toUpper();
            for (int k = 0; k < 3; ++k)
                m_charToRegion.push_back(region);
        }

        line += "  ";
        for (size_t j = 0; j < 16; ++j) {
            size_t idx = i + j;
            if (idx >= dataSize)
                break;
            uint8_t c = data[idx];
            line += (c >= 32 && c <= 126) ? QChar(c) : QChar('.');
            m_charToRegion.push_back(NP_COMM);
        }

        line += "<br>";
        out += line;
    }
    return out;
}

void NpBindDialog::updateHexView() {
    if (m_listBodies->currentRow() < 0)
        return;
    const NPBindBody& body = m_npfile.Bodies()[m_listBodies->currentRow()];

    m_combinedHex.clear();
    m_currentRegions.clear();

    auto append = [&](const NPBindEntryRaw& e, HexRegion type) {
        size_t offset = m_combinedHex.size();
        m_combinedHex.insert(m_combinedHex.end(), e.data.begin(), e.data.end());
        m_currentRegions.push_back({offset, e.data.size(), type});
    };

    append(body.npcommid, NP_COMM);
    append(body.trophy, TROPHY);
    append(body.unk1, UNK1);
    append(body.unk2, UNK2);

    m_hexView->setHtml(formatHexWithRegions(m_combinedHex, m_currentRegions));

    // Update details
    // m_lblType->setText(QString("0x%1").arg(body.npcommid.type, 2, 16, QChar('0')).toUpper());
    m_lblSize->setText(QString::number(body.npcommid.size));
    m_lblNPCommID->setText(QString::fromLatin1(
        reinterpret_cast<const char*>(body.npcommid.data.data()), (int)body.npcommid.data.size()));
    m_lblTrophy->setText(body.trophy.data.empty() ? ""
                                                  : QString::number((uint8_t)body.trophy.data[0]));
}

void NpBindDialog::onBodySelected(int /*row*/) {
    updateHexView();
}

void NpBindDialog::onExportJson() {
    QString path = QFileDialog::getSaveFileName(this, tr("Export JSON"), "npbind.json",
                                                tr("JSON Files (*.json)"));
    if (path.isEmpty())
        return;

    QJsonObject root;
    QJsonObject hdr;
    // hdr["magic"] = QString("0x%1").arg(m_npfile.Header().magic, 8, 16, QChar('0'));
    hdr["version"] = (int)m_npfile.Header().version;
    hdr["file_size"] = QString::number(m_npfile.Header().file_size);
    hdr["entry_size"] = QString::number(m_npfile.Header().entry_size);
    hdr["num_entries"] = QString::number(m_npfile.Header().num_entries);
    root["header"] = hdr;

    QJsonArray bodiesArray;
    for (const auto& b : m_npfile.Bodies()) {
        QJsonObject bo;
        bo["npcomm_type"] = (int)b.npcommid.type;
        bo["npcomm_size"] = (int)b.npcommid.size;
        bo["npcomm"] = QString::fromLatin1(reinterpret_cast<const char*>(b.npcommid.data.data()),
                                           (int)b.npcommid.data.size());
        bo["trophy_type"] = (int)b.trophy.type;
        bo["trophy_size"] = (int)b.trophy.size;
        bo["trophy_id"] = b.trophy.data.empty() ? QJsonValue() : QJsonValue((int)b.trophy.data[0]);

        bo["unk1_hex"] = QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(b.unk1.data.data()), (int)b.unk1.data.size())
                .toHex());
        bo["unk2_hex"] = QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(b.unk2.data.data()), (int)b.unk2.data.size())
                .toHex());

        bodiesArray.append(bo);
    }
    root["bodies"] = bodiesArray;
    root["filename"] = QFileInfo(m_filePath).fileName();
    root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["sha1"] = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(m_npfile.Digest()), 20).toHex());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot write file"));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    QMessageBox::information(this, tr("Done"), tr("JSON exported successfully."));
}

void NpBindDialog::onHexContextMenuRequested(const QPoint& pos) {
    QMenu contextMenu(this);
    QAction* copyAction = contextMenu.addAction("Copy Hex");
    QAction* exportAction = contextMenu.addAction("Export to JSON");

    QAction* selectedAction = contextMenu.exec(m_hexView->mapToGlobal(pos));
    if (!selectedAction)
        return;

    if (selectedAction == copyAction) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        QTextCursor cursor = m_hexView->textCursor();
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();

        QString selectedHex;
        for (int i = start; i < end && i < (int)m_combinedHex.size(); ++i)
            selectedHex += QString("%1 ").arg(m_combinedHex[i], 2, 16, QChar('0')).toUpper();
        clipboard->setText(selectedHex);
    } else if (selectedAction == exportAction) {
        onExportJson();
    }
}
