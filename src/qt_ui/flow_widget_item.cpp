// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "flow_widget_item.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOption>

FlowWidgetItem::FlowWidgetItem(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover); // We need to enable the hover attribute to ensure that hover events
                                // are handled.
}

FlowWidgetItem::~FlowWidgetItem() {}

void FlowWidgetItem::PolishStyle() {
    style()->unpolish(this);
    style()->polish(this);
}

void FlowWidgetItem::paintEvent(QPaintEvent* /*event*/) {
    // Needed for stylesheets to apply to QWidgets
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    if (!got_visible && cb_on_first_visibility) {
        if (QWidget* widget = static_cast<QWidget*>(parent())) {
            if (widget->visibleRegion().intersects(geometry())) {
                got_visible = true;
                cb_on_first_visibility();
            }
        }
    }
}

void FlowWidgetItem::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);

    // We need to polish the widgets in order to re-apply any stylesheet changes for the focus
    // property.
    PolishStyle();

    Q_EMIT Focused();
}

void FlowWidgetItem::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);

    // We need to polish the widgets in order to re-apply any stylesheet changes for the focus
    // property.
    PolishStyle();
}

void FlowWidgetItem::keyPressEvent(QKeyEvent* event) {
    if (!event) {
        return;
    }

    switch (event->key()) {
    case Qt::Key_Left:
        Q_EMIT Navigate(flow_navigation::left);
        return;
    case Qt::Key_Right:
        Q_EMIT Navigate(flow_navigation::right);
        return;
    case Qt::Key_Up:
        Q_EMIT Navigate(flow_navigation::up);
        return;
    case Qt::Key_Down:
        Q_EMIT Navigate(flow_navigation::down);
        return;
    case Qt::Key_Home:
        Q_EMIT Navigate(flow_navigation::home);
        return;
    case Qt::Key_End:
        Q_EMIT Navigate(flow_navigation::end);
        return;
    case Qt::Key_PageUp:
        Q_EMIT Navigate(flow_navigation::page_up);
        return;
    case Qt::Key_PageDown:
        Q_EMIT Navigate(flow_navigation::page_down);
        return;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

bool FlowWidgetItem::event(QEvent* event) {
    bool hover_changed = false;

    switch (event->type()) {
    case QEvent::HoverEnter:
        hover_changed = setProperty("hover", "true");
        break;
    case QEvent::HoverLeave:
        hover_changed = setProperty("hover", "false");
        break;
    default:
        break;
    }

    if (hover_changed) {
        // We need to polish the widgets in order to re-apply any stylesheet changes for the custom
        // hover property. :hover does not work if we add descendants in the qss, so we need to use
        // a custom property.
        PolishStyle();
    }

    return QWidget::event(event);
}
