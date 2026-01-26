// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "game_item.h"
#include "game_list_delegate.h"
#include "gui_settings.h"

#include <QHeaderView>

GameListDelegate::GameListDelegate(QObject* parent) : TableItemDelegate(parent, true) {}

void GameListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    TableItemDelegate::paint(painter, option, index);

    // Find out if the icon or size items are visible
    if (index.column() == static_cast<int>(GUI::GameListColumns::dir_size) ||
        (m_has_icons && index.column() == static_cast<int>(GUI::GameListColumns::icon))) {
        if (const QTableWidget* table = static_cast<const QTableWidget*>(parent())) {
            // We need to remove the headers from our calculation. The visualItemRect starts at 0,0
            // while the visibleRegion doesn't.
            QRegion visible_region = table->visibleRegion();
            visible_region.translate(-table->verticalHeader()->width(),
                                     -table->horizontalHeader()->height());

            if (const QTableWidgetItem* current_item = table->item(index.row(), index.column());
                current_item &&
                visible_region.boundingRect().intersects(table->visualItemRect(current_item))) {
                if (GameItem* item = static_cast<GameItem*>(
                        table->item(index.row(), static_cast<int>(GUI::GameListColumns::icon)))) {
                    if (index.column() == static_cast<int>(GUI::GameListColumns::dir_size)) {
                        if (!item->getSizeOnDiskLoading()) {
                            item->getSizeCalcFunc();
                        }
                    } else if (m_has_icons &&
                               index.column() == static_cast<int>(GUI::GameListColumns::icon)) {
                        if (!item->getIconLoading()) {
                            item->getIconLoadFunc(index.row());
                        }
                    }
                }
            }
        }
    }
}
