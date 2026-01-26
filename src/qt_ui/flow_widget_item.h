// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QKeyEvent>
#include <QWidget>

#include <functional>

enum class flow_navigation { up, down, left, right, home, end, page_up, page_down };

class FlowWidgetItem : public QWidget {
    Q_OBJECT

    Q_PROPERTY(
        bool hover MEMBER m_hover) // Stylesheet workaround for descendants with parent pseudo state
    Q_PROPERTY(bool selected MEMBER
                   selected) // Stylesheet workaround for descendants with parent pseudo state

public:
    FlowWidgetItem(QWidget* parent);
    virtual ~FlowWidgetItem();

    virtual void PolishStyle();

    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

    bool got_visible{};
    bool selected{};
    std::function<void()> cb_on_first_visibility{};

protected:
    bool m_hover{};

Q_SIGNALS:
    void Navigate(flow_navigation value);
    void Focused();
};
