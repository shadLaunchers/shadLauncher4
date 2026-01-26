// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "game_list_grid_item.h"

#include <QStyle>
#include <QVBoxLayout>

GameListGridItem::GameListGridItem(QWidget* parent, game_info game, const QString& title)
    : FlowWidgetItem(parent), GameItemBase(), m_game(std::move(game)) {
    setObjectName("GameListGridItem");
    setAttribute(Qt::WA_Hover); // We need to enable the hover attribute to ensure that hover events
                                // are handled.

    cb_on_first_visibility = [this]() {
        if (!getIconLoading()) {
            getIconLoadFunc(0);
        }
    };

    m_icon_label = new QLabel(this);
    m_icon_label->setObjectName("GameListGridItem_icon_label");
    m_icon_label->setAttribute(Qt::WA_TranslucentBackground);
    m_icon_label->setScaledContents(true);

    m_title_label = new QLabel(title, this);
    m_title_label->setObjectName("GameListGridItem_title_label");
    m_title_label->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
    m_title_label->setWordWrap(true);
    m_title_label->setVisible(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_icon_label, 1);
    layout->addWidget(m_title_label, 0);

    setLayout(layout);
}

void GameListGridItem::SetIconSize(const QSize& size) {
    m_icon_size = size;
}

void GameListGridItem::SetIcon(const QPixmap& pixmap) {
    m_icon_size = pixmap.size() / devicePixelRatioF();
    m_icon_label->setPixmap(pixmap);
}

void GameListGridItem::AdjustSize() {
    m_icon_label->setMinimumSize(m_icon_size);
    m_icon_label->setMaximumSize(m_icon_size);
    m_title_label->setMaximumWidth(m_icon_size.width());
}

void GameListGridItem::ShowTitle(bool visible) {
    if (m_title_label) {
        m_title_label->setVisible(visible);
    }
}

void GameListGridItem::PolishStyle() {
    FlowWidgetItem::PolishStyle();

    m_title_label->style()->unpolish(m_title_label);
    m_title_label->style()->polish(m_title_label);
}

bool GameListGridItem::event(QEvent* event) {
    return FlowWidgetItem::event(event);
}
