// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "flow_widget.h"

#include <QPainter>
#include <QScrollArea>
#include <QStyleOption>
#include <QVBoxLayout>

FlowWidget::FlowWidget(QWidget* parent) : QWidget(parent) {
    m_flow_layout = new FlowLayout();

    QWidget* widget = new QWidget(this);
    widget->setLayout(m_flow_layout);
    widget->setObjectName("flow_widget_content");
    widget->setFocusProxy(this);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidget(widget);
    m_scroll_area->setWidgetResizable(true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_scroll_area);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

FlowWidget::~FlowWidget() {}

void FlowWidget::AddWidget(FlowWidgetItem* widget) {
    if (widget) {
        m_widgets.push_back(widget);
        m_flow_layout->addWidget(widget);

        connect(widget, &FlowWidgetItem::Navigate, this, &FlowWidget::OnNavigate);
        connect(widget, &FlowWidgetItem::Focused, this, &FlowWidget::OnItemFocus);
    }
}

void FlowWidget::Clear() {
    m_widgets.clear();
    m_flow_layout->clear();
}

FlowWidgetItem* FlowWidget::SelectedItem() const {
    if (m_selected_index >= 0 && static_cast<u64>(m_selected_index) < m_widgets.size()) {
        return m_widgets.at(m_selected_index);
    }

    return nullptr;
}

s64 FlowWidget::FindItem(const FlowLayout::position& pos) {
    if (pos.row < 0 || pos.col < 0) {
        return -1;
    }

    const auto& positions = m_flow_layout->positions();

    for (s64 i = 0; i < positions.size(); i++) {
        const auto& other = positions.at(i);
        if (other.row == pos.row && other.col == pos.col) {
            return i;
        }
    }

    return -1;
}

FlowLayout::position FlowWidget::FindItem(FlowWidgetItem* item) {
    if (item) {
        const auto& item_list = m_flow_layout->item_list();
        const auto& positions = m_flow_layout->positions();

        for (s64 i = 0; i < item_list.size(); i++) {
            const auto& layout_item = item_list.at(i);
            if (layout_item && layout_item->widget() == item) {
                return positions.at(i);
            }
        }
    }

    return FlowLayout::position{.row = -1, .col = -1};
}

FlowLayout::position FlowWidget::FindNextItem(FlowLayout::position current_pos,
                                              flow_navigation value) {
    if (current_pos.row >= 0 && current_pos.col >= 0 && m_flow_layout->rows() > 0 &&
        m_flow_layout->cols() > 0) {
        switch (value) {
        case flow_navigation::up:
            // Go up one row.
            if (current_pos.row > 0) {
                current_pos.row--;
            }
            break;
        case flow_navigation::down:
            // Go down one row. Beware of last row which might have less columns.
            for (const auto& pos : m_flow_layout->positions()) {
                ;
                if (pos.col != current_pos.col)
                    continue;
                if (pos.row == current_pos.row + 1) {
                    current_pos.row = pos.row;
                    break;
                }
            }
            break;
        case flow_navigation::left:
            // Go left one column.
            if (current_pos.col > 0) {
                current_pos.col--;
            }
            break;
        case flow_navigation::right:
            // Go right one column. Beware of last row which might have less columns.
            for (const auto& pos : m_flow_layout->positions()) {
                if (pos.row > current_pos.row)
                    break;
                if (pos.row < current_pos.row)
                    continue;
                if (pos.col == current_pos.col + 1) {
                    current_pos.col = pos.col;
                    break;
                }
            }
            break;
        case flow_navigation::home:
            // Go to leftmost column.
            current_pos.col = 0;
            break;
        case flow_navigation::end:
            // Go to last column. Beware of last row which might have less columns.
            for (const auto& pos : m_flow_layout->positions()) {
                if (pos.row > current_pos.row)
                    break;
                if (pos.row < current_pos.row)
                    continue;
                current_pos.col = std::max(current_pos.col, pos.col);
            }
            break;
        case flow_navigation::page_up:
            // Go to top row.
            current_pos.row = 0;
            break;
        case flow_navigation::page_down:
            // Go to bottom row. Beware of last row which might have less columns.
            for (const auto& pos : m_flow_layout->positions()) {
                if (pos.col != current_pos.col)
                    continue;
                current_pos.row = std::max(current_pos.row, pos.row);
            }
            break;
        }
    }

    return current_pos;
}

void FlowWidget::SelectItem(FlowWidgetItem* item) {
    const FlowLayout::position selected_pos = FindItem(item);
    const s64 selected_index = FindItem(selected_pos);

    if (selected_index < 0 || static_cast<u64>(selected_index) >= Items().size()) {
        m_selected_index = -1;
        return;
    }

    m_selected_index = selected_index;
    Q_EMIT ItemSelectionChanged(m_selected_index);

    for (u64 i = 0; i < Items().size(); i++) {
        if (FlowWidgetItem* item = Items().at(i)) {
            // We need to polish the widgets in order to re-apply any stylesheet changes for the
            // selected property.
            item->selected = m_selected_index >= 0 && i == static_cast<u64>(m_selected_index);
            item->PolishStyle();
        }
    }

    // Make sure we see the focused widget
    m_scroll_area->ensureWidgetVisible(Items().at(m_selected_index));
}

void FlowWidget::OnItemFocus() {
    SelectItem(static_cast<FlowWidgetItem*>(QObject::sender()));
}

void FlowWidget::OnNavigate(flow_navigation value) {
    const FlowLayout::position selected_pos =
        FindNextItem(FindItem(static_cast<FlowWidgetItem*>(QObject::sender())), value);
    const s64 selected_index = FindItem(selected_pos);
    if (selected_index < 0 || static_cast<u64>(selected_index) >= Items().size()) {
        return;
    }

    if (FlowWidgetItem* item = Items().at(selected_index)) {
        item->setFocus();
    }

    m_selected_index = selected_index;
}

void FlowWidget::mouseDoubleClickEvent(QMouseEvent* ev) {
    if (!ev)
        return;

    // Qt's itemDoubleClicked signal doesn't distinguish between mouse buttons and there is no
    // simple way to get the pressed button. So we have to ignore this event when another button is
    // pressed.
    if (ev->button() != Qt::LeftButton) {
        ev->ignore();
        return;
    }

    QWidget::mouseDoubleClickEvent(ev);
}
