// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QHeaderView>
#include <QScrollBar>
#include <QStringBuilder>
#include "common/fs_util.h"
#include "custom_table_widget_item.h"
#include "game_list_delegate.h"
#include "game_list_frame.h"
#include "game_list_table.h"
#include "gui_settings.h"
#include "localized.h"
#include "persistent_settings.h"
#include "qt_utils.h"

GameListTable::GameListTable(GameListFrame* frame, std::shared_ptr<GUISettings> gui_settings,
                             std::shared_ptr<PersistentSettings> persistent_settings)
    : GameList(), m_game_list_frame(frame), m_gui_settings(std::move(gui_settings)),
      m_persistent_settings(std::move(persistent_settings)) {
    m_is_list_layout = true;

    setShowGrid(false);
    setItemDelegate(new GameListDelegate(this));
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalScrollBar()->setSingleStep(20);
    horizontalScrollBar()->setSingleStep(20);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setVisible(false);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setSortIndicatorShown(true);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setDefaultSectionSize(150);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setColumnCount(static_cast<int>(GUI::GameListColumns::count));
    setMouseTracking(true);

    connect(this, &GameListTable::sizeOnDiskReady, this,
            [this](const game_info& game, GameItemBase* item) {
                if (!game || !game->item || game->item != item)
                    return;
                if (QTableWidgetItem* size_item =
                        this->item(static_cast<GameItem*>(game->item)->row(),
                                   static_cast<int>(GUI::GameListColumns::dir_size))) {
                    const u64& game_size = game->info.size_on_disk;
                    size_item->setText(game_size != UINT64_MAX
                                           ? GUI::Utils::FormatByteSize(game_size)
                                           : tr("Unknown"));
                    size_item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(game_size));
                }
            });

    connect(this, &GameList::IconReady, this,
            [this](const game_info& game, const GameItemBase* item) {
                if (game && item && game->item == item)
                    item->getImageChangeCallback();
            });
}

void GameListTable::restoreLayout(const QByteArray& state) {
    // Resize to fit and get the ideal icon column width
    resizeColumnsToContents();
    const int icon_column_width = columnWidth(static_cast<int>(GUI::GameListColumns::icon));

    // Restore header layout from last session
    if (!horizontalHeader()->restoreState(state) && rowCount()) {
        // Nothing to do
    }

    // Make sure no columns are squished
    FixNarrowColumns();

    // Make sure that the icon column is large enough for the actual items.
    // This is important if the list appeared as empty when closing the software before.
    horizontalHeader()->resizeSection(static_cast<int>(GUI::GameListColumns::icon),
                                      icon_column_width);

    // Save new header state
    horizontalHeader()->restoreState(horizontalHeader()->saveState());
}

void GameListTable::resizeColumnsToContents(int spacing) {
    horizontalHeader()->resizeSections(QHeaderView::ResizeMode::ResizeToContents);

    // Make non-icon columns slightly bigger for better visuals
    for (int i = 1; i < columnCount(); i++) {
        if (isColumnHidden(i)) {
            continue;
        }

        const int size = horizontalHeader()->sectionSize(i) + spacing;
        horizontalHeader()->resizeSection(i, size);
    }
}

void GameListTable::adjustIconColumn() {
    // Fixate vertical header and row height
    verticalHeader()->setDefaultSectionSize(m_icon_size.height());
    verticalHeader()->setMinimumSectionSize(m_icon_size.height());
    verticalHeader()->setMaximumSectionSize(m_icon_size.height());

    // Resize the icon column
    resizeColumnToContents(static_cast<int>(GUI::GameListColumns::icon));

    // Shorten the last section to remove horizontal scrollbar if possible
    resizeColumnToContents(static_cast<int>(GUI::GameListColumns::count) - 1);
}

void GameListTable::sort(u64 game_count, int sort_column, Qt::SortOrder col_sort_order) {
    // Back-up old header sizes to handle unwanted column resize in case of zero search results
    const int old_row_count = rowCount();
    const u64 old_game_count = game_count;

    std::vector<int> column_widths(columnCount());
    for (int i = 0; i < columnCount(); i++) {
        column_widths[i] = columnWidth(i);
    }

    // Sorting resizes hidden columns, so unhide them as a workaround
    std::vector<int> columns_to_hide;

    for (int i = 0; i < columnCount(); i++) {
        if (isColumnHidden(i)) {
            setColumnHidden(i, false);
            columns_to_hide.push_back(i);
        }
    }

    // Sort the list by column and sort order
    sortByColumn(sort_column, col_sort_order);

    // Hide columns again
    for (int col : columns_to_hide) {
        setColumnHidden(col, true);
    }

    // Don't resize the columns if no game is shown to preserve the header settings
    if (!rowCount()) {
        for (int i = 0; i < columnCount(); i++) {
            setColumnWidth(i, column_widths[i]);
        }

        horizontalHeader()->setSectionResizeMode(static_cast<int>(GUI::GameListColumns::icon),
                                                 QHeaderView::Fixed);
        return;
    }

    // Fixate vertical header and row height
    verticalHeader()->setDefaultSectionSize(m_icon_size.height());
    verticalHeader()->setMinimumSectionSize(m_icon_size.height());
    verticalHeader()->setMaximumSectionSize(m_icon_size.height());

    // Resize columns if the game list was empty before
    if (!old_row_count && !old_game_count) {
        resizeColumnsToContents();
    } else {
        resizeColumnToContents(static_cast<int>(GUI::GameListColumns::icon));
    }

    // Fixate icon column
    horizontalHeader()->setSectionResizeMode(static_cast<int>(GUI::GameListColumns::icon),
                                             QHeaderView::Fixed);

    // Shorten the last section to remove horizontal scrollbar if possible
    resizeColumnToContents(static_cast<int>(GUI::GameListColumns::count) - 1);
}

void GameListTable::SetCustomConfigIcon(const game_info& game) {
    if (!game) {
        return;
    }

    const QString serial = QString::fromStdString(game->info.serial);

    for (int row = 0; row < rowCount(); ++row) {
        if (QTableWidgetItem* title_item =
                item(row, static_cast<int>(GUI::GameListColumns::name))) {
            if (const QTableWidgetItem* serial_item =
                    item(row, static_cast<int>(GUI::GameListColumns::serial));
                serial_item && serial_item->text() == serial) {
                title_item->setIcon(GameListBase::GetCustomConfigIcon(game));
            }
        }
    }
}

void GameListTable::Populate(const std::vector<game_info>& game_data,
                             const std::map<QString, QString>& notes_map,
                             const std::map<QString, QString>& title_map,
                             const std::string& selected_item_id) {
    ClearList();

    setRowCount(static_cast<int>(game_data.size()));

    // Default locale. Uses current Qt application language.
    const QLocale locale{};
    const Localized localized;

    int row = 0;
    int index = -1;
    int selected_row = -1;

    const auto get_title = [&title_map](const QString& serial, const std::string& name) -> QString {
        if (const auto it = title_map.find(serial); it != title_map.cend()) {
            return it->second;
        }

        return QString::fromStdString(name);
    };

    for (const auto& game : game_data) {
        index++;

        const QString serial = QString::fromStdString(game->info.serial);
        const QString title = get_title(serial, game->info.name);

        // Icon
        CustomTableWidgetItem* icon_item = new CustomTableWidgetItem;
        game->item = icon_item;

        icon_item->setImageChangeCallback([this, icon_item, game]() {
            if (!icon_item || !game) {
                return;
            }

            std::lock_guard lock(icon_item->pixmap_mutex);

            if (!game->pxmap.isNull()) {
                icon_item->setData(Qt::DecorationRole, game->pxmap);
                game->pxmap = {};
            }
        });

        icon_item->setSizeCalcFunc(
            [this, game, cancel = icon_item->getSizeOnDiskLoadingAborted()]() {
                if (!game || game->info.size_on_disk != UINT64_MAX || (cancel && cancel->load()))
                    return;

                // Calculate main game folder size
                uint64_t total_size = FS::Utils::GetDirSize(game->info.path, 1, cancel.get());

                // Check for "-UPDATE" and "-PATCH" folders
                for (const auto& suffix : {"-UPDATE", "-patch"}) {
                    std::filesystem::path extra_path = game->info.path;
                    extra_path += suffix;

                    if (std::filesystem::exists(extra_path) && (!cancel || !cancel->load())) {
                        total_size += FS::Utils::GetDirSize(extra_path.string(), 1, cancel.get());
                        break; // if update founds don't search for -patch
                    }
                }

                game->info.size_on_disk = total_size;

                if (!cancel || !cancel->load()) {
                    Q_EMIT sizeOnDiskReady(game, game->item);
                }
            });

        icon_item->setData(Qt::UserRole, index, true);
        icon_item->setData(GUI::CustomRoles::game_role, QVariant::fromValue(game));

        // Title
        CustomTableWidgetItem* title_item = new CustomTableWidgetItem(title);
        title_item->setIcon(GameListBase::GetCustomConfigIcon(game));

        // Serial
        CustomTableWidgetItem* serial_item = new CustomTableWidgetItem(game->info.serial);

        if (const auto it = notes_map.find(serial);
            it != notes_map.cend() && !it->second.isEmpty()) {
            const QString tool_tip = QString("%0 [%1]\n\n%2\n%3")
                                         .arg(title)
                                         .arg(serial)
                                         .arg(tr("Notes:"))
                                         .arg(it->second);
            title_item->setToolTip(tool_tip);
            serial_item->setToolTip(tool_tip);
        }

        // Compatibility
        CustomTableWidgetItem* compat_item = new CustomTableWidgetItem;
        compat_item->setText(game->compat.text);

        compat_item->setData(Qt::UserRole, game->compat.index, true);
        if (game->compat.index <= 4) {
            QString tooltip_string =
                "<p>" + tr("Last updated") +
                QString(": %1 (%2)")
                    .arg(game->compat.last_tested_date, game->compat.latest_version) +
                "<br>" + game->compat.tooltip + "</p>";
            compat_item->setToolTip(tooltip_string);
        } else {
            compat_item->setToolTip(game->compat.tooltip);
        }
        if (!game->compat.color.isEmpty()) {
            compat_item->setData(
                Qt::DecorationRole,
                GUI::Utils::CirclePixmap(game->compat.color, devicePixelRatioF() * 2));
        }

        CustomTableWidgetItem* region_item = new CustomTableWidgetItem;
        QImage scaledPixmap;
        if (game->info.region == "Japan") {
            scaledPixmap = QImage(":images/flag_jp.png");
            region_item->setToolTip(tr("Japan"));
        } else if (game->info.region == "Europe") {
            scaledPixmap = QImage(":images/flag_eu.png");
            region_item->setToolTip(tr("Europe"));
        } else if (game->info.region == "USA") {
            scaledPixmap = QImage(":images/flag_us.png");
            region_item->setToolTip(tr("USA"));
        } else if (game->info.region == "Asia") {
            scaledPixmap = QImage(":images/flag_china.png");
            region_item->setToolTip(tr("Asia"));
        } else if (game->info.region == "World") {
            scaledPixmap = QImage(":images/flag_world.png");
            region_item->setToolTip(tr("World"));
        } else {
            scaledPixmap = QImage(":images/flag_unk.png");
            region_item->setToolTip(tr("Unknown"));
        }
        QPixmap pixmap = QPixmap::fromImage(
            scaledPixmap.scaled(64 * devicePixelRatioF(), 44 * devicePixelRatioF(),
                                Qt::KeepAspectRatio, Qt::SmoothTransformation));

        pixmap.setDevicePixelRatio(devicePixelRatioF());
        region_item->setData(Qt::DecorationRole, pixmap);
        region_item->setData(Qt::UserRole, region_item->toolTip(),
                             true); // make it sortable by region name

        // Playtimes
        const quint64 elapsed_ms = m_persistent_settings->GetPlaytime(serial);

        // Last played (support outdated values)
        QDateTime last_played;
        const QString last_played_str = m_persistent_settings->GetLastPlayed(serial);

        if (!last_played_str.isEmpty()) {
            last_played =
                QDateTime::fromString(last_played_str, GUI::Persistent::last_played_date_format);

            if (!last_played.isValid()) {
                last_played = QDateTime::fromString(last_played_str,
                                                    GUI::Persistent::last_played_date_format_old);
            }
        }

        const u64 game_size = game->info.size_on_disk;

        setItem(row, static_cast<int>(GUI::GameListColumns::icon), icon_item);
        setItem(row, static_cast<int>(GUI::GameListColumns::name), title_item);
        setItem(row, static_cast<int>(GUI::GameListColumns::compat), compat_item);
        setItem(row, static_cast<int>(GUI::GameListColumns::serial), serial_item);
        setItem(row, static_cast<int>(GUI::GameListColumns::region), region_item);
        double fw_value = std::stod(game->info.fw);
        auto* fw_item = new CustomTableWidgetItem(QString::fromStdString(game->info.fw),
                                                  Qt::UserRole, QVariant(fw_value));
        setItem(row, static_cast<int>(GUI::GameListColumns::firmware), fw_item);

        double app_value = std::stod(game->info.app_ver);
        auto* app_item = new CustomTableWidgetItem(QString::fromStdString(game->info.app_ver),
                                                   Qt::UserRole, QVariant(app_value));
        setItem(row, static_cast<int>(GUI::GameListColumns::version), app_item);

        setItem(row, static_cast<int>(GUI::GameListColumns::last_play),
                new CustomTableWidgetItem(
                    locale.toString(last_played,
                                    last_played >= QDateTime::currentDateTime().addDays(-7)
                                        ? GUI::Persistent::last_played_date_with_time_of_day_format
                                        : GUI::Persistent::last_played_date_format_new),
                    Qt::UserRole, last_played));
        setItem(row, static_cast<int>(GUI::GameListColumns::play_time),
                new CustomTableWidgetItem(
                    elapsed_ms == 0 ? tr("Never played") : localized.getVerboseTimeByMs(elapsed_ms),
                    Qt::UserRole, elapsed_ms));
        setItem(row, static_cast<int>(GUI::GameListColumns::dir_size),
                new CustomTableWidgetItem(
                    game_size != UINT64_MAX ? GUI::Utils::FormatByteSize(game_size) : tr("Unknown"),
                    Qt::UserRole, QVariant::fromValue<qulonglong>(game_size)));
        setItem(row, static_cast<int>(GUI::GameListColumns::path),
                new CustomTableWidgetItem(game->info.path));

        if (selected_item_id == game->info.path + game->info.icon_path) {
            selected_row = row;
        }

        row++;
    }

    selectRow(selected_row);
}

void GameListTable::RepaintIcons(std::vector<game_info>& game_data, const QColor& icon_color,
                                 const QSize& icon_size, qreal device_pixel_ratio) {
    GameListBase::RepaintIcons(game_data, icon_color, icon_size, device_pixel_ratio);
    adjustIconColumn();
}

void GameListTable::paintEvent(QPaintEvent* event) {
    QPainter painter(viewport()); // <-- paint directly on the visible viewport
    float opacity = static_cast<float>(
        m_gui_settings->GetValue(GUI::game_list_backgroundImageOpacity).toInt() / 100.f);
    painter.setOpacity(opacity);

    // Draw background first
    if (!m_game_list_frame->backgroundImage.isNull() &&
        m_gui_settings->GetValue(GUI::game_list_showBackgroundImage).toBool()) {
        QPixmap scaledPixmap = QPixmap::fromImage(m_game_list_frame->backgroundImage)
                                   .scaled(viewport()->size(), Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);

        int x = (viewport()->width() - scaledPixmap.width()) / 2;
        int y = (viewport()->height() - scaledPixmap.height()) / 2;
        painter.drawPixmap(x, y, scaledPixmap);
    }

    // Now draw the table contents on top
    QTableWidget::paintEvent(event);
}
