// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QPaintEvent>
#include <QScrollArea>
#include <QWidget>
#include "common/types.h"
#include "flow_layout.h"
#include "flow_widget_item.h"

class FlowWidget : public QWidget {
    Q_OBJECT

public:
    FlowWidget(QWidget* parent);
    virtual ~FlowWidget();

    void AddWidget(FlowWidgetItem* widget);
    void Clear();

    std::vector<FlowWidgetItem*>& Items() {
        return m_widgets;
    }
    FlowWidgetItem* SelectedItem() const;
    QScrollArea* ScrollArea() const {
        return m_scroll_area;
    }

Q_SIGNALS:
    void ItemSelectionChanged(int index);

private Q_SLOTS:
    void OnItemFocus();
    void OnNavigate(flow_navigation value);

protected:
    void SelectItem(FlowWidgetItem* item);
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    s64 FindItem(const FlowLayout::position& pos);
    FlowLayout::position FindItem(FlowWidgetItem* item);
    FlowLayout::position FindNextItem(FlowLayout::position current_pos, flow_navigation value);

    FlowLayout* m_flow_layout{};
    QScrollArea* m_scroll_area{};
    std::vector<FlowWidgetItem*> m_widgets;
    s64 m_selected_index = -1;
};
