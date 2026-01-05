// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVector>
#include "core/file_format/npbind.h"

enum HexRegion { NP_COMM, TROPHY, UNK1, UNK2 };

struct HexSegment {
    size_t offset;
    size_t size;
    HexRegion type;
};

class NpBindDialog : public QDialog {
    Q_OBJECT
public:
    explicit NpBindDialog(QWidget* parent = nullptr, const QString& filePath = QString());

private slots:
    void onBodySelected(int row);
    void onExportJson();
    void updateHexView();
    void onHexContextMenuRequested(const QPoint& pos);

private:
    void populateBodiesList();
    QString formatHexWithRegions(const std::vector<uint8_t>& data,
                                 const std::vector<HexSegment>& regions);
    HexRegion regionAt(int pos) const;
    QString regionName(HexRegion r) const;
    QColor colorForRegion(HexRegion r) const;
    bool isRegionVisible(HexRegion r) const;

    QString m_filePath;

    // UI Elements
    QListWidget* m_listBodies = nullptr;
    QLabel* m_lblType = nullptr;
    QLabel* m_lblSize = nullptr;
    QLabel* m_lblNPCommID = nullptr;
    QLabel* m_lblTrophy = nullptr;
    QTextEdit* m_hexView = nullptr;
    QPushButton* m_btnExport = nullptr;
    QCheckBox* m_chkNPComm = nullptr;
    QCheckBox* m_chkTrophy = nullptr;
    QCheckBox* m_chkUnk1 = nullptr;
    QCheckBox* m_chkUnk2 = nullptr;

    // Data
    NPBindFile m_npfile;
    std::vector<uint8_t> m_combinedHex;
    std::vector<HexSegment> m_currentRegions;
    std::vector<HexRegion> m_charToRegion;
};