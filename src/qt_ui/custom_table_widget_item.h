// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "game_item.h"

class CustomTableWidgetItem : public GameItem {
private:
    int m_sort_role = Qt::DisplayRole;

public:
    using QTableWidgetItem::setData;

    CustomTableWidgetItem() = default;
    CustomTableWidgetItem(const std::string& text, int sort_role = Qt::DisplayRole,
                          const QVariant& sort_value = 0);
    CustomTableWidgetItem(const QString& text, int sort_role = Qt::DisplayRole,
                          const QVariant& sort_value = 0);

    bool operator<(const QTableWidgetItem& other) const override;

    void setData(int role, const QVariant& value, bool assign_sort_role);
};
