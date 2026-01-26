// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "table_item_delegate.h"

class GameListDelegate : public TableItemDelegate {
public:
    explicit GameListDelegate(QObject* parent);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};
