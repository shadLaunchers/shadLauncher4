// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QStringBuilder>

#include "game_list_grid.h"
#include "game_list_grid_item.h"
#include "gui_settings.h"
#include "stylesheets.h"

GameListGrid::GameListGrid(GameListFrame* frame, std::shared_ptr<GUISettings> gui_settings)
    : m_game_list_frame(frame), m_gui_settings(std::move(gui_settings)), FlowWidget(nullptr),
      GameListBase() {
    setObjectName("game_list_grid");
    setContextMenuPolicy(Qt::CustomContextMenu);
    setStyleSheet(
        GUI::Stylesheets::default_style_sheet); // todo: check why it's not applying w/o this

    m_icon_ready_callback = [this](const game_info& game, const GameItemBase* item) {
        Q_EMIT IconReady(game, item);
    };

    connect(
        this, &GameListGrid::IconReady, this,
        [this](const game_info& game, const GameItemBase* item) {
            if (game && item && game->item == item)
                item->getImageChangeCallback();
        },
        Qt::QueuedConnection); // The default 'AutoConnection' doesn't seem to work in this specific
                               // case...

    connect(this, &FlowWidget::ItemSelectionChanged, this, [this](int index) {
        if (GameListGridItem* item = static_cast<GameListGridItem*>(Items().at(index))) {
            Q_EMIT ItemSelectionChanged(item->Game());
        }
    });
}

void GameListGrid::ClearList() {
    Clear();
}

void GameListGrid::Populate(const std::vector<game_info>& game_data,
                            const std::map<QString, QString>& notes_map,
                            const std::map<QString, QString>& title_map,
                            const std::string& selected_item_id) {
    ClearList();

    GameListGridItem* selected_item = nullptr;

    blockSignals(true);

    const auto get_title = [&title_map](const QString& serial, const std::string& name) -> QString {
        if (const auto it = title_map.find(serial); it != title_map.cend()) {
            return it->second.simplified();
        }

        return QString::fromStdString(name).simplified();
    };

    for (const auto& game : game_data) {
        const QString serial = QString::fromStdString(game->info.serial);
        const QString title = get_title(serial, game->info.name);

        GameListGridItem* item = new GameListGridItem(this, game, title);
        item->installEventFilter(this);
        item->setFocusPolicy(Qt::StrongFocus);

        game->item = item;

        if (const auto it = notes_map.find(serial);
            it != notes_map.cend() && !it->second.isEmpty()) {
            item->setToolTip(QString("%0 [%1]\n\n%2\n%3")
                                 .arg(title)
                                 .arg(serial)
                                 .arg(tr("Notes:"))
                                 .arg(it->second));
        } else {
            item->setToolTip(QString("%0 [%1]").arg(title).arg(serial));
        }

        item->setImageChangeCallback([this, item, game]() {
            if (!item || !game) {
                return;
            }
            std::lock_guard lock(item->pixmap_mutex);

            if (!game->pxmap.isNull()) {
                item->SetIcon(game->pxmap);
                game->pxmap = {};
            }
        });

        if (selected_item_id == game->info.path + game->info.icon_path) {
            selected_item = item;
        }

        AddWidget(item);
    }

    blockSignals(false);

    // Update layout before setting focus on the selected item
    show();

    QApplication::processEvents();

    SelectItem(selected_item);
}

void GameListGrid::RepaintIcons(std::vector<game_info>& game_data, const QColor& icon_color,
                                const QSize& icon_size, qreal device_pixel_ratio) {
    m_icon_size = icon_size;
    m_icon_color = icon_color;

    QPixmap placeholder(icon_size * device_pixel_ratio);
    placeholder.setDevicePixelRatio(device_pixel_ratio);
    placeholder.fill(Qt::transparent);

    const bool show_title =
        m_icon_size.width() >
        (GUI::game_list_icon_size_medium.width() + GUI::game_list_icon_size_small.width()) / 2;

    for (game_info& game : game_data) {
        if (GameListGridItem* item = static_cast<GameListGridItem*>(game->item)) {
            if (item->getIconLoading()) {
                // We already have an icon. Simply set the icon size to let the label scale itself
                // in a quick and dirty fashion.
                item->SetIconSize(m_icon_size);
            } else {
                // We don't have an icon. Set a placeholder to initialize the layout.
                game->pxmap = placeholder;
                item->getImageChangeCallback();
            }

            item->setIconLoadFunc(
                [this, game, device_pixel_ratio, cancel = item->getIconLoadingAborted()](int) {
                    IconLoadFunction(game, device_pixel_ratio, cancel);
                });

            item->AdjustSize();
            item->ShowTitle(show_title);
            item->got_visible = false;
        }
    }
}

void GameListGrid::FocusAndSelectFirstEntryIfNoneIs() {
    if (!Items().empty()) {
        Items().front()->setFocus();
    }
}

bool GameListGrid::eventFilter(QObject* watched, QEvent* event) {
    if (!event) {
        return false;
    }

    if (event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        if (GameListGridItem* item = static_cast<GameListGridItem*>(watched)) {
            Q_EMIT ItemDoubleClicked(item->Game());
            return true;
        }
    }

    return false;
}

void GameListGrid::keyPressEvent(QKeyEvent* event) {
    if (!event) {
        return;
    }

    const auto modifiers = event->modifiers();

    if (modifiers == Qt::ControlModifier && event->key() == Qt::Key_F && !event->isAutoRepeat()) {
        Q_EMIT FocusToSearchBar();
        return;
    }

    FlowWidget::keyPressEvent(event);
}

void GameListGrid::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    float opacity = static_cast<float>(
        m_gui_settings->GetValue(GUI::game_list_backgroundImageOpacity).toInt() / 100.f);
    painter.setOpacity(opacity);

    // Draw background first
    if (!m_game_list_frame->backgroundImage.isNull() &&
        m_gui_settings->GetValue(GUI::game_list_showBackgroundImage).toBool()) {
        QPixmap scaledPixmap =
            QPixmap::fromImage(m_game_list_frame->backgroundImage)
                .scaled(rect().size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int x = (rect().width() - scaledPixmap.width()) / 2;
        int y = (rect().height() - scaledPixmap.height()) / 2;
        painter.drawPixmap(x, y, scaledPixmap);
    }

    QWidget::paintEvent(event);
}
