// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <vector>
#include <QAbstractTableModel>

struct PkgInfo {
    QString title;
    QString serial;
    QString category;
    QString app_version;
    std::filesystem::path filepath;
};

class PkgInstallModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Columns { Install = 0, TitleId, Name, AppVersion, Category, ColumnCount };

    explicit PkgInstallModel(QObject* parent = nullptr);

    void setPkgs(std::vector<PkgInfo> pkgs);
    std::vector<PkgInfo> selectedPkgs() const;
    bool hasSelection() const;

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

private:
    std::vector<PkgInfo> m_pkgs;
    std::vector<bool> m_checked;
};
