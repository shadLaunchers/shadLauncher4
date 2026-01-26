// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pkg_install_model.h"

PkgInstallModel::PkgInstallModel(QObject* parent) : QAbstractTableModel(parent) {}

void PkgInstallModel::setPkgs(std::vector<PkgInfo> pkgs) {
    beginResetModel();
    m_pkgs = std::move(pkgs);
    m_checked.assign(m_pkgs.size(), true); // default checked
    endResetModel();
}

int PkgInstallModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(m_pkgs.size());
}

int PkgInstallModel::columnCount(const QModelIndex&) const {
    return ColumnCount;
}

QVariant PkgInstallModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case Install:
        return tr("Install");
    case TitleId:
        return tr("Title ID");
    case Name:
        return tr("Game Name");
    case AppVersion:
        return tr("App Version");
    case Category:
        return tr("Category");
    default:
        return {};
    }
}

QVariant PkgInstallModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};

    const auto& pkg = m_pkgs[index.row()];

    if (index.column() == Install) {
        if (role == Qt::CheckStateRole)
            return m_checked[index.row()] ? Qt::Checked : Qt::Unchecked;
        return {};
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TitleId:
            return pkg.serial;
        case Name:
            return pkg.title;
        case AppVersion:
            return pkg.app_version;
        case Category:
            return pkg.category;
        }
    }

    return {};
}

Qt::ItemFlags PkgInstallModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    if (index.column() == Install)
        f |= Qt::ItemIsUserCheckable;

    return f;
}

bool PkgInstallModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (index.column() == Install && role == Qt::CheckStateRole) {
        m_checked[index.row()] = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    return false;
}

bool PkgInstallModel::hasSelection() const {
    for (bool v : m_checked)
        if (v)
            return true;
    return false;
}

std::vector<PkgInfo> PkgInstallModel::selectedPkgs() const {
    std::vector<PkgInfo> out;
    for (size_t i = 0; i < m_pkgs.size(); ++i)
        if (m_checked[i])
            out.push_back(m_pkgs[i]);
    return out;
}
