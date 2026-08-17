// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <core/user_settings.h>
#include <fmt/core.h>
#include "background_music_player.h"
#include "change_log_dialog.h"
#include "common/key_manager.h"
#include "common/singleton.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/file_format/psf.h"
#include "core/file_sys/game_backend.h"
#include "core/file_sys/zar_packer.h"
#include "core/ipc/ipc_client.h"
#include "game_categories.h"
#include "game_list_context_menu.h"
#include "game_list_frame.h"
#include "game_list_grid.h"
#include "game_list_grid_item.h"
#include "game_list_table.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "localized.h"
#include "npbind_dialog.h"
#include "progress_dialog.h"
#include "qt_utils.h"

#ifdef _WIN32
#include <string>
#include <vector>
#include <QDebug>
#include <QStringList>
#include <objbase.h>
#include <shlguid.h>
#include <shobjidl.h>
#include <windows.h>
#include <winternl.h>
#include <wrl/client.h>
#endif

#include "cheats_patches_dialog.h"
#include "common/log_analyzer.h"
#include "common/path_util.h"
#include "core/ipc/ipc_client.h"
#include "settings_dialog.h"
#include "sfo_viewer_dialog.h"
#include "trophy_viewer.h"
#include "zarchive_viewer_dialog.h"

GameListFrame::GameListFrame(std::shared_ptr<GUISettings> gui_settings,
                             std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                             std::shared_ptr<IpcClient> ipc_client, QWidget* parent)
    : CustomDockWidget(tr("Game List"), parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emu_settings)), m_ipc_client(std::move(ipc_client)) {

    m_icon_size = GUI::game_list_icon_size_min; // ensure a valid size
    m_is_list_layout = m_gui_settings->GetValue(GUI::game_list_listMode).toBool();
    m_margin_factor = m_gui_settings->GetValue(GUI::game_list_marginFactor).toReal();
    m_text_factor = m_gui_settings->GetValue(GUI::game_list_textFactor).toReal();
    m_icon_color = m_gui_settings->GetValue(GUI::game_list_iconColor).value<QColor>();
    m_col_sort_order = m_gui_settings->GetValue(GUI::game_list_sortAsc).toBool()
                           ? Qt::AscendingOrder
                           : Qt::DescendingOrder;
    m_sort_column = m_gui_settings->GetValue(GUI::game_list_sortCol).toInt();
    m_hidden_list =
        GUI::Utils::ListToSet(m_gui_settings->GetValue(GUI::game_list_hidden_list).toStringList());

    m_old_layout_is_list = m_is_list_layout;

    m_info_cache = std::make_shared<GameInfoCache>(
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_info_cache.sqlite3");
    QThreadPool::globalInstance()->start([cache = m_info_cache]() { cache->WarmUp(); });

    // Categories are stored in the game info database, so this has to come
    // after the cache is up.
    m_categories = std::make_shared<GameCategories>(m_info_cache);
    m_current_category =
        m_categories->Resolve(m_gui_settings->GetValue(GUI::game_list_current_category).toString());

    // Save factors for first setup
    m_gui_settings->SetValue(GUI::game_list_iconColor, m_icon_color, false);
    m_gui_settings->SetValue(GUI::game_list_marginFactor, m_margin_factor, false);
    m_gui_settings->SetValue(GUI::game_list_textFactor, m_text_factor, true);

    m_game_dock = new QMainWindow(this);
    m_game_dock->setWindowFlags(Qt::FramelessWindowHint | Qt::Widget);
    setWidget(m_game_dock);

    m_game_grid = new GameListGrid(this, m_gui_settings);
    m_game_grid->installEventFilter(this);
    m_game_grid->ScrollArea()->verticalScrollBar()->installEventFilter(this);

    m_game_list = new GameListTable(this, m_gui_settings);
    m_game_list->installEventFilter(this);
    m_game_list->verticalScrollBar()->installEventFilter(this);

    m_game_compat = new GameCompatibility(m_gui_settings, this);

    m_central_widget = new QStackedWidget(this);
    m_central_widget->addWidget(m_game_list);
    m_central_widget->addWidget(m_game_grid);

    if (m_is_list_layout) {
        m_central_widget->setCurrentWidget(m_game_list);
    } else {
        m_central_widget->setCurrentWidget(m_game_grid);
    }

    // One tab per category, sitting right above the list/grid.
    m_category_tabs = new QTabBar(this);
    m_category_tabs->setExpanding(false);
    m_category_tabs->setDrawBase(false);
    m_category_tabs->setUsesScrollButtons(true);
    m_category_tabs->setElideMode(Qt::ElideRight);
    m_category_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_category_tabs->setToolTip(tr("Right click a tab to add, rename or delete a category.\n"
                                   "Use the game's right click menu to put it into a category."));

    QWidget* game_area = new QWidget(this);
    QVBoxLayout* game_area_layout = new QVBoxLayout(game_area);
    game_area_layout->setContentsMargins(0, 0, 0, 0);
    game_area_layout->setSpacing(0);
    game_area_layout->addWidget(m_category_tabs);
    game_area_layout->addWidget(m_central_widget);

    RebuildCategoryTabs();

    splitter = new QSplitter(Qt::Vertical);
    logDisplay = new QTextEdit(splitter);

    QPalette logPalette = logDisplay->palette();
    logPalette.setColor(QPalette::Base, Qt::black);
    logPalette.setColor(QPalette::Text, Qt::white);
    logDisplay->setPalette(logPalette);
    logDisplay->setText(tr("Game Log"));
    logDisplay->setReadOnly(true);

    splitter->addWidget(game_area);
    splitter->addWidget(logDisplay);

    QList<int> sizes =
        m_gui_settings->Var2IntList(m_gui_settings->GetValue(GUI::main_window_dockWidgetSizes));
    splitter->setSizes({sizes});
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    m_game_dock->setCentralWidget(splitter);

    bool showLog = m_gui_settings->GetValue(GUI::main_window_showLog).toBool();
    showLog ? logDisplay->show() : logDisplay->hide();

    // Actions regarding showing/hiding columns
    auto add_column = [this](GUI::GameListColumns col, const QString& header_text,
                             const QString& action_text) {
        m_game_list->setHorizontalHeaderItem(static_cast<int>(col),
                                             new QTableWidgetItem(header_text));
        m_columnActs.append(new QAction(action_text, this));
    };

    add_column(GUI::GameListColumns::icon, tr("Icon"), tr("Show Icons"));
    add_column(GUI::GameListColumns::name, tr("Name"), tr("Show Names"));
    add_column(GUI::GameListColumns::compat, tr("Compatibility"), tr("Show Compatibility"));
    add_column(GUI::GameListColumns::serial, tr("Serial"), tr("Show Serials"));
    add_column(GUI::GameListColumns::region, tr("Region"), tr("Show Regions"));
    add_column(GUI::GameListColumns::firmware, tr("Firmware"), tr("Show Firmwares"));
    add_column(GUI::GameListColumns::version, tr("Version"), tr("Show Versions"));
    add_column(GUI::GameListColumns::last_play, tr("Last Played"), tr("Show Last Played"));
    add_column(GUI::GameListColumns::play_time, tr("Time Played"), tr("Show Time Played"));
    add_column(GUI::GameListColumns::dir_size, tr("Space On Disk"), tr("Show Space On Disk"));
    add_column(GUI::GameListColumns::path, tr("Path"), tr("Show Paths"));

    m_progress_dialog = new ProgressDialog(
        tr("Loading games"), tr("Loading games, please wait..."), tr("Cancel"), 0, 0, false, this,
        Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    m_progress_dialog->setMinimumDuration(INT_MAX);

    CreateConnections();

    m_game_list->CreateHeaderActions(
        m_columnActs,
        [this](int col) {
            return m_gui_settings->GetGamelistColVisibility(static_cast<GUI::GameListColumns>(col));
        },
        [this](int col, bool visible) {
            m_gui_settings->SetGamelistColVisibility(static_cast<GUI::GameListColumns>(col),
                                                     visible);
        });
}

GameListFrame::~GameListFrame() {
    WaitAndAbortSizeCalcThreads();
    WaitAndAbortRepaintThreads();
    GUI::Utils::StopFutureWatcher(m_parsing_watcher, true);
    GUI::Utils::StopFutureWatcher(m_refresh_watcher, true);

    QList<int> sizes = splitter->sizes();
    m_gui_settings->SetValue(GUI::main_window_dockWidgetSizes, QVariant::fromValue(sizes));
}

void GameListFrame::LoadSettings() {
    m_col_sort_order = m_gui_settings->GetValue(GUI::game_list_sortAsc).toBool()
                           ? Qt::AscendingOrder
                           : Qt::DescendingOrder;
    m_sort_column = m_gui_settings->GetValue(GUI::game_list_sortCol).toInt();
    m_draw_compat_status_to_grid = m_gui_settings->GetValue(GUI::game_list_draw_compat).toBool();

    // Categories live in the settings too, so pick up external changes (e.g. a
    // settings reset) and rebuild the tabs from them.
    if (m_categories) {
        m_categories->Load();
        m_current_category = m_categories->Resolve(
            m_gui_settings->GetValue(GUI::game_list_current_category).toString());
        RebuildCategoryTabs();
    }

    m_game_list->SyncHeaderActions(m_columnActs, [this](int col) {
        return m_gui_settings->GetGamelistColVisibility(static_cast<GUI::GameListColumns>(col));
    });
}

void GameListFrame::SaveSettings() {
    for (int col = 0; col < m_columnActs.count(); ++col) {
        m_gui_settings->SetGamelistColVisibility(static_cast<GUI::GameListColumns>(col),
                                                 m_columnActs[col]->isChecked());
    }
    m_gui_settings->SetValue(GUI::game_list_sortCol, m_sort_column, false);
    m_gui_settings->SetValue(GUI::game_list_sortAsc, m_col_sort_order == Qt::AscendingOrder, false);
    m_gui_settings->SetValue(GUI::game_list_state, m_game_list->horizontalHeader()->saveState(),
                             true);
}

void GameListFrame::CreateConnections() {
    connect(m_game_list->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &GameListFrame::OnColumnClicked);
    connect(m_game_list, &GameList::FocusToSearchBar, this, &GameListFrame::FocusToSearchBar);
    connect(m_game_grid, &GameListGrid::FocusToSearchBar, this, &GameListFrame::FocusToSearchBar);

    // progress bar
    connect(m_progress_dialog, &QProgressDialog::canceled, this, [this]() {
        GUI::Utils::StopFutureWatcher(m_parsing_watcher, true);
        GUI::Utils::StopFutureWatcher(m_refresh_watcher, true);

        m_path_entries.clear();
        m_path_list.clear();
        m_game_keys.clear();
        m_game_data.clear();
        m_notes.clear();
        m_titles.clear();
        m_games.pop_all();
        {
            std::lock_guard lock(m_pending_cache_puts_mutex);
            m_pending_cache_puts.clear();
        }
    });

    connect(&m_parsing_watcher, &QFutureWatcher<void>::finished, this,
            &GameListFrame::OnParsingFinished);
    connect(&m_parsing_watcher, &QFutureWatcher<void>::canceled, this, [this]() {
        WaitAndAbortSizeCalcThreads();
        WaitAndAbortRepaintThreads();

        m_path_entries.clear();
        m_path_list.clear();
        m_game_data.clear();
        m_game_keys.clear();
        m_games.pop_all();
        {
            std::lock_guard lock(m_pending_cache_puts_mutex);
            m_pending_cache_puts.clear();
        }
    });
    connect(&m_refresh_watcher, &QFutureWatcher<void>::finished, this,
            &GameListFrame::OnRefreshFinished);
    connect(&m_refresh_watcher, &QFutureWatcher<void>::canceled, this, [this]() {
        WaitAndAbortSizeCalcThreads();
        WaitAndAbortRepaintThreads();

        m_path_entries.clear();
        m_path_list.clear();
        m_game_data.clear();
        m_game_keys.clear();
        m_games.pop_all();
        {
            std::lock_guard lock(m_pending_cache_puts_mutex);
            m_pending_cache_puts.clear();
        }

        if (m_progress_dialog) {
            m_progress_dialog->accept();
        }
    });
    connect(&m_refresh_watcher, &QFutureWatcher<void>::progressRangeChanged, this,
            [this](int minimum, int maximum) {
                if (m_progress_dialog) {
                    m_progress_dialog->SetRange(minimum, maximum);
                }
            });
    connect(&m_refresh_watcher, &QFutureWatcher<void>::progressValueChanged, this,
            [this](int value) {
                if (m_progress_dialog) {
                    m_progress_dialog->SetValue(value);
                }
            });
    // category tabs
    connect(m_category_tabs, &QTabBar::currentChanged, this, &GameListFrame::OnCategoryTabChanged);
    connect(m_category_tabs, &QWidget::customContextMenuRequested, this,
            &GameListFrame::ShowCategoryTabContextMenu);
    connect(m_categories.get(), &GameCategories::Changed, this, [this]() {
        RebuildCategoryTabs();
        Refresh(false, {}, false);
    });

    // context menu and clicks
    connect(m_game_list, &QTableWidget::customContextMenuRequested, this,
            &GameListFrame::ShowContextMenu);
    connect(m_game_list, &QTableWidget::itemDoubleClicked, this,
            QOverload<QTableWidgetItem*>::of(&GameListFrame::DoubleClickedSlot));
    connect(m_game_list, &QTableWidget::itemSelectionChanged, this, [this]() {
        game_info game = nullptr;
        if (const auto item = m_game_list->item(m_game_list->currentRow(),
                                                static_cast<int>(GUI::GameListColumns::icon));
            item && item->isSelected()) {
            game = GetGameInfoByMode(item);
            PlayBackgroundMusic(game);
            QImage bg(QString::fromUtf8(game->info.pic_path.c_str()));
            if (!bg.isNull()) {
                backgroundImage = bg;
                m_game_list->update();
            }
        }
        Q_EMIT NotifyGameSelection(game);
    });

    connect(m_game_grid, &QWidget::customContextMenuRequested, this,
            &GameListFrame::ShowContextMenu);
    connect(m_game_grid, &GameListGrid::ItemDoubleClicked, this,
            QOverload<const game_info&>::of(&GameListFrame::DoubleClickedSlot));
    connect(m_game_grid, &GameListGrid::ItemSelectionChanged, this, [this](game_info game) {
        PlayBackgroundMusic(game);
        QImage bg(QString::fromUtf8(game->info.pic_path.c_str()));
        if (!bg.isNull()) {
            backgroundImage = bg;
            m_game_grid->update();
        }
        Q_EMIT NotifyGameSelection(game);
    });

    // compatibility list connections
    connect(m_game_compat, &GameCompatibility::DownloadStarted, this, [this]() {
        for (const auto& game : m_game_data) {
            game->compat = m_game_compat->GetStatusData("Download");
        }
        UpdateCompatColumn();
    });
    connect(m_game_compat, &GameCompatibility::DownloadFinished, this,
            &GameListFrame::OnCompatFinished);
    connect(m_game_compat, &GameCompatibility::DownloadCanceled, this,
            &GameListFrame::OnCompatFinished);
    connect(m_game_compat, &GameCompatibility::DownloadError, this, [this](const QString& error) {
        OnCompatFinished();
        QMessageBox::warning(
            this, tr("Warning!"),
            tr("Failed to retrieve the online compatibility database!\nUsing local database.\n\n%0")
                .arg(error));
    });
}

void GameListFrame::OnColumnClicked(int col) {
    if (col == static_cast<int>(GUI::GameListColumns::icon))
        return; // Don't "sort" icons.

    if (col == m_sort_column) {
        m_col_sort_order =
            (m_col_sort_order == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    } else {
        m_col_sort_order = Qt::AscendingOrder;
    }
    m_sort_column = col;

    m_gui_settings->SetValue(GUI::game_list_sortAsc, m_col_sort_order == Qt::AscendingOrder, false);
    m_gui_settings->SetValue(GUI::game_list_sortCol, col, true);

    m_game_list->sort(m_game_data.size(), m_sort_column, m_col_sort_order);
}

bool GameListFrame::SearchMatchesTitle(QString title_name, bool fallback) const {
    if (!m_search_text.isEmpty()) {
        QString search_text = m_search_text.toLower();

        // Ignore trademarks when no search results have been yielded by unmodified search
        static const QRegularExpression s_ignored_on_fallback(
            reinterpret_cast<const char*>(u8"[:\\-®©™]+"));

        if (fallback) {
            search_text = search_text.simplified();
            title_name = title_name.simplified();

            QString title_name_replaced_trademarks_with_spaces = title_name;
            QString title_name_simplified = title_name;

            search_text.remove(s_ignored_on_fallback);
            title_name.remove(s_ignored_on_fallback);
            title_name_replaced_trademarks_with_spaces.replace(s_ignored_on_fallback, " ");

            // Before simplify to allow spaces in the beginning and end where ignored characters may
            // have been
            if (title_name_replaced_trademarks_with_spaces.contains(search_text)) {
                return true;
            }

            title_name_replaced_trademarks_with_spaces =
                title_name_replaced_trademarks_with_spaces.simplified();

            if (title_name_replaced_trademarks_with_spaces.contains(search_text)) {
                return true;
            }

            // Initials-only search
            if (search_text.size() >= 2 &&
                search_text.count(QRegularExpression(QStringLiteral("[a-z0-9]"))) >= 2 &&
                !search_text.contains(QRegularExpression(QStringLiteral("[^a-z0-9 ]")))) {
                QString initials = QStringLiteral("\\b");

                for (auto it = search_text.begin(); it != search_text.end(); it++) {
                    if (it->isSpace()) {
                        continue;
                    }

                    initials += *it;
                    initials += QStringLiteral("\\w*\\b ");
                }

                initials += QChar('?');

                if (title_name_replaced_trademarks_with_spaces.contains(
                        QRegularExpression(initials))) {
                    return true;
                }
            }
        }

        return title_name.contains(search_text);
    }
    return true;
}

bool GameListFrame::SearchMatchesApp(const game_info& game, bool fallback) const {
    if (m_search_text.isEmpty()) {
        return true;
    }

    const QString original_title = QString::fromStdString(game->info.name).toLower();
    if (SearchMatchesTitle(original_title, fallback)) {
        return true;
    }

    // A renamed game has to stay findable under both names, so check the custom
    // title as well as the one from param.sfo.
    if (const auto it = m_titles.find(GUI::Utils::GameKeyOf(game->info)); it != m_titles.cend()) {
        const QString custom_title = it->second.toLower();
        if (custom_title != original_title && SearchMatchesTitle(custom_title, fallback)) {
            return true;
        }
    }

    return QString::fromStdString(game->info.serial).toLower().contains(m_search_text.toLower());
}

bool GameListFrame::IsEntryVisible(const game_info& game, bool search_fallback) const {
    const bool is_visible =
        m_show_hidden || !m_hidden_list.contains(GUI::Utils::GameKeyOf(game->info));
    return is_visible && MatchesCurrentCategory(game) && SearchMatchesApp(game, search_fallback);
}

bool GameListFrame::MatchesCurrentCategory(const game_info& game) const {
    // The "All" tab has no category attached to it and shows everything.
    if (m_current_category.isEmpty() || !m_categories) {
        return true;
    }
    if (!game) {
        return false;
    }
    return m_categories->IsInCategory(GameCategories::KeyFor(game->info), m_current_category);
}

void GameListFrame::RebuildCategoryTabs() {
    if (!m_category_tabs) {
        return;
    }

    // A category may have been deleted or renamed under our feet.
    if (!m_current_category.isEmpty() && !m_categories->Contains(m_current_category)) {
        m_current_category.clear();
        m_gui_settings->SetValue(GUI::game_list_current_category, m_current_category);
    }

    // Don't let the rebuild itself trigger a category change.
    m_updating_category_tabs = true;

    while (m_category_tabs->count() > 0) {
        m_category_tabs->removeTab(0);
    }

    const int all_tab = m_category_tabs->addTab(tr("All"));
    m_category_tabs->setTabData(all_tab, QString());
    m_category_tabs->setTabToolTip(all_tab, tr("Every game found in your game folders"));

    int index_to_select = all_tab;

    for (const QString& category : m_categories->Names()) {
        // Escape ampersands so they don't turn into a mnemonic.
        const int tab = m_category_tabs->addTab(QString(category).replace('&', "&&"));
        m_category_tabs->setTabData(tab, category);
        m_category_tabs->setTabToolTip(
            tab, tr("%n game(s) in this category", "", m_categories->CountIn(category)));

        if (category == m_current_category) {
            index_to_select = tab;
        }
    }

    m_category_tabs->setCurrentIndex(index_to_select);

    m_updating_category_tabs = false;
}

void GameListFrame::OnCategoryTabChanged(int index) {
    if (m_updating_category_tabs || !m_category_tabs) {
        return;
    }

    const QString category = index >= 0 ? m_category_tabs->tabData(index).toString() : QString();
    if (category == m_current_category) {
        return;
    }

    m_current_category = category;
    m_gui_settings->SetValue(GUI::game_list_current_category, m_current_category);
    Refresh(false, {}, false);
}

void GameListFrame::SetCurrentCategory(const QString& category) {
    if (!m_category_tabs) {
        return;
    }

    const QString resolved = m_categories->Resolve(category);
    for (int i = 0; i < m_category_tabs->count(); ++i) {
        if (m_category_tabs->tabData(i).toString() == resolved) {
            m_category_tabs->setCurrentIndex(i);
            return;
        }
    }
}

QString GameListFrame::PromptNewCategory(const GameKey* key) {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Category"), tr("Category name:"),
                                               QLineEdit::Normal, QString(), &accepted)
                             .trimmed();

    if (!accepted || name.isEmpty()) {
        return {};
    }

    if (m_categories->Contains(name)) {
        const QString existing = m_categories->Resolve(name);
        QMessageBox::information(this, tr("Category Already Exists"),
                                 tr("A category named \"%1\" already exists.").arg(existing));
        if (key && !key->IsNull()) {
            m_categories->SetMembership(*key, existing, true);
        }
        return existing;
    }

    if (!m_categories->Create(name)) {
        return {};
    }

    if (key && !key->IsNull()) {
        m_categories->SetMembership(*key, name, true);
    }
    return name;
}

void GameListFrame::ShowCategoryTabContextMenu(const QPoint& pos) {
    if (!m_category_tabs) {
        return;
    }

    const int tab = m_category_tabs->tabAt(pos);
    const QString category = tab >= 0 ? m_category_tabs->tabData(tab).toString() : QString();

    QMenu menu(this);
    QAction* new_category = menu.addAction(tr("&New Category..."));

    QAction* rename_category = nullptr;
    QAction* delete_category = nullptr;
    if (!category.isEmpty()) {
        menu.addSeparator();
        rename_category = menu.addAction(tr("&Rename \"%1\"").arg(category));
        delete_category = menu.addAction(tr("&Delete \"%1\"").arg(category));
    }

    QAction* chosen = menu.exec(m_category_tabs->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == new_category) {
        PromptNewCategory();
    } else if (chosen == rename_category) {
        bool accepted = false;
        const QString name =
            QInputDialog::getText(this, tr("Rename Category"), tr("Category name:"),
                                  QLineEdit::Normal, category, &accepted)
                .trimmed();
        if (!accepted || name.isEmpty() || name == category) {
            return;
        }
        const bool was_current = m_current_category == category;
        if (was_current) {
            m_current_category = name;
        }

        if (m_categories->Rename(category, name)) {
            if (was_current) {
                m_gui_settings->SetValue(GUI::game_list_current_category, m_current_category);
            }
        } else {
            if (was_current) {
                m_current_category = category;
            }
            QMessageBox::information(this, tr("Category Already Exists"),
                                     tr("A category named \"%1\" already exists.").arg(name));
        }
    } else if (chosen == delete_category) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("Delete Category"),
            tr("Delete the category \"%1\"? The games in it are not touched.").arg(category),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_categories->Remove(category);
        }
    }
}

void GameListFrame::ResetCustomTitles() {
    if (m_titles.empty()) {
        return;
    }

    m_titles.clear();
    if (m_info_cache) {
        m_info_cache->ClearTitles();
    }

    Refresh();
}

void GameListFrame::SetShowHidden(bool show) {
    m_show_hidden = show;
}

void GameListFrame::closeEvent(QCloseEvent* event) {
    SaveSettings();

    QDockWidget::closeEvent(event);
    Q_EMIT GameListFrameClosed();
}

void GameListFrame::FocusAndSelectFirstEntryIfNoneIs() {
    if (m_is_list_layout) {
        if (m_game_list) {
            m_game_list->FocusAndSelectFirstEntryIfNoneIs();
        }
    } else {
        if (m_game_grid) {
            m_game_grid->FocusAndSelectFirstEntryIfNoneIs();
        }
    }
}

std::string GameListFrame::CurrentSelectionPath() {
    std::string selection;

    game_info game{};

    if (m_old_layout_is_list) {
        if (!m_game_list->selectedItems().isEmpty()) {
            if (QTableWidgetItem* item = m_game_list->item(m_game_list->currentRow(), 0)) {
                if (const QVariant var = item->data(GUI::game_role); var.canConvert<game_info>()) {
                    game = var.value<game_info>();
                }
            }
        }
    } else if (m_game_grid) {
        if (GameListGridItem* item = static_cast<GameListGridItem*>(m_game_grid->SelectedItem())) {
            game = item->Game();
        }
    }

    if (game) {
        selection = game->info.path + game->info.icon_path;
    }

    m_old_layout_is_list = m_is_list_layout;

    return selection;
}

void GameListFrame::WaitAndAbortRepaintThreads() {
    for (const game_info& game : m_game_data) {
        if (game && game->item) {
            game->item->waitForIconLoading(true);
        }
    }
}

void GameListFrame::WaitAndAbortSizeCalcThreads() {
    for (const game_info& game : m_game_data) {
        if (game && game->item) {
            game->item->waitForSizeOnDiskLoading(true);
        }
    }
}

void GameListFrame::ResizeIcons(const int& slider_pos) {
    m_icon_size_index = slider_pos;
    m_icon_size = GUISettings::GetSizeFromSlider(slider_pos);

    RepaintIcons();
}

void GameListFrame::ShowCustomConfigIcon(const game_info& game) {
    if (!game) {
        return;
    }

    const std::string serial = game->info.serial;
    const bool has_custom_config = game->has_custom_config;
    const bool has_custom_pad_config = game->has_custom_pad_config;

    for (const auto& other_game : m_game_data) {
        if (other_game->info.serial == serial) {
            other_game->has_custom_config = has_custom_config;
            other_game->has_custom_pad_config = has_custom_pad_config;
        }
    }

    m_game_list->SetCustomConfigIcon(game);

    RepaintIcons();
}

void GameListFrame::SetShowCompatibilityInGrid(bool show) {
    m_draw_compat_status_to_grid = show;
    RepaintIcons();
    m_gui_settings->SetValue(GUI::game_list_draw_compat, show);
}

const std::vector<game_info>& GameListFrame::GetGameInfo() const {
    return m_game_data;
}

void GameListFrame::CheckCompatibilityAtStartup() {
    if (m_gui_settings->GetValue(GUI::compatibility_check_on_startup).toBool()) {
        m_game_compat->RequestCompatibility(true);
    }
}

void GameListFrame::SetListMode(const bool& is_list) {
    m_old_layout_is_list = m_is_list_layout;
    m_is_list_layout = is_list;

    m_gui_settings->SetValue(GUI::game_list_listMode, is_list);

    Refresh();

    if (m_is_list_layout) {
        m_central_widget->setCurrentWidget(m_game_list);
    } else {
        m_central_widget->setCurrentWidget(m_game_grid);
    }
}

void GameListFrame::SetSearchText(const QString& text) {
    m_search_text = text;
    Refresh();
}

void GameListFrame::RepaintIcons(const bool& from_settings) {
    GUI::Utils::StopFutureWatcher(m_parsing_watcher, false);
    GUI::Utils::StopFutureWatcher(m_refresh_watcher, false);
    WaitAndAbortRepaintThreads();

    if (from_settings) {
        if (m_gui_settings->GetValue(GUI::meta_enableUIColors).toBool()) {
            m_icon_color = m_gui_settings->GetValue(GUI::game_list_iconColor).value<QColor>();
        } else {
            m_icon_color = GUI::Utils::GetLabelColor("gamelist_icon_background_color",
                                                     Qt::transparent, Qt::transparent);
        }
    }

    if (m_is_list_layout) {
        m_game_list->RepaintIcons(m_game_data, m_icon_color, m_icon_size, devicePixelRatioF());
    } else {
        m_game_grid->SetDrawCompatStatusToGrid(m_draw_compat_status_to_grid);
        m_game_grid->RepaintIcons(m_game_data, m_icon_color, m_icon_size, devicePixelRatioF());
    }
}

void GameListFrame::PushPath(const std::string& path, std::vector<std::string>& legit_paths) {
    {
        std::lock_guard lock(m_path_mutex);
        if (!m_path_list.insert(path).second) {
            return;
        }
    }
    legit_paths.push_back(path);
}

void GameListFrame::OnParsingFinished() {
    const Localized localized;

    // Remove duplicates
    sort(m_path_entries.begin(), m_path_entries.end(),
         [](const path_entry& l, const path_entry& r) { return l.path < r.path; });
    m_path_entries.erase(
        unique(m_path_entries.begin(), m_path_entries.end(),
               [](const path_entry& l, const path_entry& r) { return l.path == r.path; }),
        m_path_entries.end());

    const s32 language_index = GUIApplication::getLanguageId();
    const std::string localized_title = fmt::format("TITLE_%02d", language_index);
    const std::string localized_icon = fmt::format("ICON0_%02d.PNG", language_index);

    auto cached_meta =
        std::make_shared<const std::unordered_map<std::string, GameInfoCache::CachedEntry>>(
            m_info_cache ? m_info_cache->GetAllMeta()
                         : std::unordered_map<std::string, GameInfoCache::CachedEntry>{});

    const auto add_game = [this, localized_title, localized_icon, language_index,
                           cached_meta](const std::string& dir_or_elf) {
        GUIGameInfo game{};
        const std::filesystem::path entry_path(dir_or_elf);
        const bool is_archive = Core::FileSys::IsZArchiveFile(entry_path);
        game.info.path = GUI::Utils::NormalizePath(entry_path);

        const Localized thread_localized;

        const std::string sfo_dir = dir_or_elf + "/sce_sys";
        std::string sfo_path = sfo_dir + "/param.sfo";
        if (is_archive) {
            if (const auto resolved =
                    Core::FileSys::ResolveGameFilePath(entry_path, "sce_sys/param.sfo")) {
                sfo_path = resolved->string();
            } else {
                qDebug() << "Failed to read sce_sys/param.sfo from archive:"
                         << QString::fromStdString(dir_or_elf);
                return;
            }
        }

        s64 fingerprint = 0;
        if (is_archive) {
            std::error_code ec;
            if (const auto ftime = std::filesystem::last_write_time(entry_path, ec); !ec) {
                fingerprint = static_cast<s64>(ftime.time_since_epoch().count()) ^
                              (static_cast<s64>(language_index) << 48);
            }
        } else if (std::error_code ec; std::filesystem::exists(sfo_path, ec) && !ec) {
            if (const auto ftime = std::filesystem::last_write_time(sfo_path, ec); !ec) {
                fingerprint = static_cast<s64>(ftime.time_since_epoch().count()) ^
                              (static_cast<s64>(language_index) << 48);
            }
        }

        if (fingerprint != 0) {
            if (const auto it = cached_meta->find(game.info.path);
                it != cached_meta->end() && it->second.fingerprint == fingerprint) {
                game.info = it->second.info; // copy: the map is shared across worker threads
            }
        }

        if (game.info.serial.empty()) {
            PSF psf;
            psf.Open(sfo_path);
            if (const auto category = psf.GetString("CATEGORY"); category.has_value()) {
                game.info.category = *category;
#ifdef _WIN32
                if (_stricmp(game.info.category.c_str(), "ac") == 0) // skip dlc
#else
                if (strcasecmp(game.info.category.c_str(), "ac") == 0)
#endif
                    return;
            }
            NPBindFile m_npfile;
            std::string npbind_path = dir_or_elf + "/sce_sys/npbind.dat";
            if (is_archive) {
                npbind_path.clear();
                if (const auto resolved =
                        Core::FileSys::ResolveGameFilePath(entry_path, "sce_sys/npbind.dat")) {
                    npbind_path = resolved->string();
                }
            }
            if (!npbind_path.empty() && m_npfile.Load(npbind_path)) {
                game.info.np_comm_ids = m_npfile.GetNpCommIds();
            }
            std::string title_id = "";
            if (const auto titleId = psf.GetString("TITLE_ID"); titleId.has_value()) {
                title_id = *titleId;
            }
            if (title_id.empty()) {
                qDebug() << "No TITLE_ID found in PARAM.SFO for path:"
                         << QString::fromStdString(dir_or_elf);
                return;
            } else {
                std::string name = "";
                if (const auto locname = psf.GetString(localized_title); locname.has_value()) {
                    name = *locname;
                }
                if (name.empty()) {
                    if (const auto defname = psf.GetString("TITLE"); defname.has_value()) {
                        name = *defname;
                    }
                }

                game.info.serial = std::string(title_id);
                game.info.name = std::string(name);
                if (const auto appversion = psf.GetString("APP_VER"); appversion.has_value()) {
                    game.info.app_ver = *appversion;
                }

                if (const auto pubtool_info = psf.GetString("PUBTOOLINFO");
                    pubtool_info.has_value()) {
                    u64 sdk_ver_offset = pubtool_info.value().find("sdk_ver");
                    if (sdk_ver_offset == pubtool_info.value().npos) {
                        game.info.sdk_ver = "0.00";
                    } else {
                        // Increment offset to account for sdk_ver= part of string.
                        sdk_ver_offset += 8;
                        u64 sdk_ver_len = pubtool_info.value().find(",", sdk_ver_offset);
                        if (sdk_ver_len == pubtool_info.value().npos) {
                            // If there's no more commas, this is likely the last entry of pubtool
                            // info. Use string length instead.
                            sdk_ver_len = pubtool_info.value().size();
                        }
                        sdk_ver_len -= sdk_ver_offset;
                        std::string sdk_ver_string =
                            pubtool_info.value().substr(sdk_ver_offset, sdk_ver_len).data();
                        // Number is stored in base 16.
                        uint32_t sdk_int = std::stoi(sdk_ver_string, nullptr, 16);
                        u8 major_bcd = (sdk_int >> 24) & 0xFF;
                        u8 minor_bcd = (sdk_int >> 16) & 0xFF;

                        int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
                        int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);

                        QString sdk = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
                        game.info.sdk_ver = sdk.toStdString();
                    }
                }

                if (const auto fw_int_opt = psf.GetInteger("SYSTEM_VER"); fw_int_opt.has_value()) {
                    uint32_t fw_int = *fw_int_opt;
                    if (fw_int == 0) {
                        game.info.fw = "0.00";
                    } else {
                        u8 major_bcd = (fw_int >> 24) & 0xFF;
                        u8 minor_bcd = (fw_int >> 16) & 0xFF;

                        int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
                        int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);

                        QString fw = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
                        game.info.fw = fw.toStdString();
                    }
                }

                if (const auto content_id = psf.GetString("CONTENT_ID");
                    content_id.has_value() && !content_id->empty()) {
                    char region = content_id->at(0);
                    switch (region) {
                    case 'U':
                        game.info.region = "USA";
                        break;
                    case 'E':
                        game.info.region = "Europe";
                        break;
                    case 'J':
                        game.info.region = "Japan";
                        break;
                    case 'H':
                        game.info.region = "Asia";
                        break;
                    case 'I':
                        game.info.region = "World";
                        break;
                    default:
                        game.info.region = "Unknown";
                        break;
                    }
                }

                if (const auto save_dir = psf.GetString("INSTALL_DIR_SAVEDATA");
                    save_dir.has_value()) {
                    game.info.save_dir = *save_dir;
                } else {
                    game.info.save_dir = game.info.serial;
                }
            }

            if (is_archive) {
                if (const auto resolved =
                        Core::FileSys::ResolveGameFilePath(entry_path, "sce_sys/PIC1.PNG")) {
                    game.info.pic_path = resolved->string();
                } else if (const auto resolved_lower =
                               Core::FileSys::ResolveGameFilePath(entry_path, "sce_sys/pic1.png")) {
                    game.info.pic_path = resolved_lower->string();
                }

                if (game.info.icon_path.empty()) {
                    if (const auto resolved = Core::FileSys::ResolveGameFilePath(
                            entry_path, "sce_sys/" + localized_icon)) {
                        game.info.icon_path = resolved->string();
                    } else if (const auto resolved_default = Core::FileSys::ResolveGameFilePath(
                                   entry_path, "sce_sys/icon0.png")) {
                        game.info.icon_path = resolved_default->string();
                    }
                }

                if (game.info.snd0_path.empty()) {
                    if (const auto resolved =
                            Core::FileSys::ResolveGameFilePath(entry_path, "sce_sys/snd0.at9")) {
                        game.info.snd0_path = resolved->string();
                    }
                }
            } else {
                game.info.pic_path = sfo_dir + "/PIC1.PNG";

                if (game.info.icon_path.empty()) {
                    if (std::string icon_path = sfo_dir + "/" + localized_icon;
                        std::filesystem::is_regular_file(icon_path)) {
                        game.info.icon_path = std::move(icon_path);
                    } else {
                        game.info.icon_path = sfo_dir + "/icon0.png";
                    }
                }

                if (game.info.snd0_path.empty()) {
                    if (std::filesystem::is_regular_file(sfo_dir + "/snd0.at9")) {
                        game.info.snd0_path = sfo_dir + "/snd0.at9";
                    }
                }
            }

            if (fingerprint != 0) {
                std::lock_guard lock(m_pending_cache_puts_mutex);
                m_pending_cache_puts.emplace_back(game.info, fingerprint);
            }
        } else {
            // Cache hit: category is already known without touching disk.
#ifdef _WIN32
            if (_stricmp(game.info.category.c_str(), "ac") == 0) // skip dlc
#else
            if (strcasecmp(game.info.category.c_str(), "ac") == 0)
#endif
                return;
        }

        const QString serial = QString::fromStdString(game.info.serial);
        const QString game_key = GUI::Utils::GameKeyOf(game.info);

        m_games_mutex.lock();

        m_game_keys.insert(game_key);

        if (QString note = m_info_cache->GetNotes(game.info.path); !note.isEmpty()) {
            m_notes.insert_or_assign(game_key, std::move(note));
        }

        if (QString title = m_info_cache->GetTitle(game.info.path); !title.isEmpty()) {
            m_titles.insert_or_assign(game_key, std::move(title));
        }

        m_games_mutex.unlock();

        game.compat = m_game_compat->GetCompatibility(game.info.serial);
        game.has_custom_config = std::filesystem::is_regular_file(
            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
            (game.info.serial + ".json"));
        game.has_custom_pad_config = std::filesystem::is_regular_file(
            Common::FS::GetUserPath(Common::FS::PathType::CustomInputConfigs) /
            (game.info.serial + ".json"));

        m_games.push(std::make_shared<GUIGameInfo>(std::move(game)));
    };

    m_refresh_watcher.setFuture(
        QtConcurrent::map(m_path_entries, [this, add_game](const path_entry& entry) {
            std::vector<std::string> legit_paths;

            const std::filesystem::path candidate_path(entry.path);
            const bool valid_dir =
                std::filesystem::is_regular_file(entry.path + "/sce_sys/param.sfo");
            const bool valid_archive =
                Core::FileSys::IsZArchiveFile(candidate_path) &&
                Core::FileSys::ReadGameFile(candidate_path, "sce_sys/param.sfo").has_value();

            // if (entry.is_from_file) { //TODO
            if (valid_dir || valid_archive) {
                PushPath(entry.path, legit_paths);
            } else {
                qDebug() << "Invalid game path registered:" << QString::fromStdString(entry.path);
                return;
            }
            // } else {
            //     PushPath(entry.path, legit_paths);
            // }

            for (const std::string& path : legit_paths) {
                add_game(path);
            }
        }));
}

void GameListFrame::OnRefreshFinished() {
    WaitAndAbortSizeCalcThreads();
    WaitAndAbortRepaintThreads();
    m_game_data.clear();

    // Move parsed results into main game data list
    for (auto&& g : m_games.pop_all()) {
        m_game_data.push_back(g);
    }

    std::vector<game_info> filtered_games;
    filtered_games.reserve(m_game_data.size());

    auto strip_zar_suffix = [](const std::string& path) {
        constexpr std::string_view suffix = ".zar";
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return path.substr(0, path.size() - suffix.size());
        }
        return path;
    };
    auto is_overlay_path = [&](const std::string& path) {
        const std::string stem = strip_zar_suffix(path);
        return stem.ends_with("-UPDATE") || stem.ends_with("-patch");
    };

    // Merge base and update game info (CUSAxxxxx + CUSAxxxxx-UPDATE) or -patch
    for (const game_info& entry : m_game_data) {
        // Skip update folders (we’ll merge them into base)
        if (is_overlay_path(entry->info.path))
            continue;

        for (const auto& other : m_game_data) {
            // Process only matching update or patch folders
            if (!is_overlay_path(other->info.path))
                continue;

            auto starts_with = [](const std::string& str, const std::string& prefix) {
                return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
            };

            const std::string base_stem = strip_zar_suffix(entry->info.path);
            const std::string other_stem = strip_zar_suffix(other->info.path);

            // Match by serial and full path prefix (including "-UPDATE" -patch folders)
            if (entry->info.serial != other->info.serial ||
                !(starts_with(other_stem, base_stem + "-UPDATE") ||
                  starts_with(other_stem, base_stem + "-patch"))) {
                continue;
            }

            // --- Directly override with update data ---
            entry->info.app_ver = other->info.app_ver;
            entry->info.fw = other->info.fw;
            entry->info.sdk_ver = other->info.sdk_ver;
            entry->info.np_comm_ids = other->info.np_comm_ids;

            entry->info.update_path = other->info.path; // Store update path
            if (!other->info.pic_path.empty() &&
                std::filesystem::is_regular_file(other->info.pic_path)) {
                entry->info.pic_path = other->info.pic_path;
            }
            if (!other->info.icon_path.empty() &&
                std::filesystem::is_regular_file(other->info.icon_path)) {
                entry->info.icon_path = other->info.icon_path;
            }
            if (!other->info.snd0_path.empty() &&
                std::filesystem::is_regular_file(other->info.snd0_path)) {
                entry->info.snd0_path = other->info.snd0_path;
            }
        }

        // Keep only base games (hide -update folders)
        filtered_games.push_back(entry);
    }

    // Replace with filtered list (no -update entries)
    m_game_data.swap(filtered_games);

    // Sort alphabetically by title (localized if available)
    std::sort(m_game_data.begin(), m_game_data.end(),
              [&](const game_info& game1, const game_info& game2) {
                  const QString key1 = GUI::Utils::GameKeyOf(game1->info);
                  const QString key2 = GUI::Utils::GameKeyOf(game2->info);
                  const QString title1 = m_titles.contains(key1)
                                             ? m_titles.at(key1)
                                             : QString::fromStdString(game1->info.name);
                  const QString title2 = m_titles.contains(key2)
                                             ? m_titles.at(key2)
                                             : QString::fromStdString(game2->info.name);
                  return title1.toLower() < title2.toLower();
              });

    // Clean up hidden games list
    m_hidden_list.intersect(m_game_keys);
    m_gui_settings->SetValue(GUI::game_list_hidden_list, QStringList(m_hidden_list.values()));
    {
        std::vector<std::pair<GameInfo, s64>> pending_puts;
        {
            std::lock_guard lock(m_pending_cache_puts_mutex);
            pending_puts.swap(m_pending_cache_puts);
        }
        std::vector<std::string> known_paths;
        known_paths.reserve(m_path_entries.size());
        for (const auto& entry : m_path_entries) {
            known_paths.push_back(GUI::Utils::NormalizePath(std::filesystem::path(entry.path)));
        }
        QThreadPool::globalInstance()->start([cache = m_info_cache,
                                              pending_puts = std::move(pending_puts),
                                              known_paths = std::move(known_paths)]() {
            cache->PutMany(pending_puts);
            cache->Prune(known_paths);
        });
    }

    m_game_keys.clear();
    m_path_list.clear();
    m_path_entries.clear();

    // Refresh UI
    Refresh();

    // Restore layout on first refresh
    if (!std::exchange(m_initial_refresh_done, true)) {
        m_game_list->restoreLayout(m_gui_settings->GetValue(GUI::game_list_state).toByteArray());
        m_game_list->SyncHeaderActions(m_columnActs, [this](int col) {
            return m_gui_settings->GetGamelistColVisibility(static_cast<GUI::GameListColumns>(col));
        });
    }

    // Notify and clean up refresh state
    Q_EMIT Refreshed();
    // m_refresh_funcs_manage_type.reset(); //TODO
    // m_refresh_funcs_manage_type.emplace();
}
#ifdef _WIN32
#ifndef FILE_SHARE_ALL
#define FILE_SHARE_ALL (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#endif

#ifndef STATUS_NO_MORE_FILES
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#endif

#ifndef _FILE_DIRECTORY_INFORMATION
#define _FILE_DIRECTORY_INFORMATION
typedef struct _FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1]; // variable length
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;
#endif

#ifndef NT_QUERY_DIRECTORY_DECLARED
extern "C" NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE FileHandle, HANDLE Event,
                                               PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                               PIO_STATUS_BLOCK IoStatusBlock,
                                               PVOID FileInformation, ULONG Length,
                                               FILE_INFORMATION_CLASS FileInformationClass,
                                               BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName,
                                               BOOLEAN RestartScan);
#define NT_QUERY_DIRECTORY_DECLARED
#endif

// UTF helpers
static std::wstring Utf8ToUtf16(const std::string& s) {
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}
static std::string Utf16ToUtf8(const std::wstring& w) {
    if (w.empty())
        return {};
    int len =
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

// Simple param.sfo existence check using CreateFileW
static bool HasParamSFO(const std::wstring& dirW) {
    std::wstring sfo = dirW;
    if (!sfo.empty() && sfo.back() != L'\\')
        sfo += L'\\';
    sfo += L"sce_sys\\param.sfo";

    HANDLE h = CreateFileW(sfo.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(h);
    return true;
}

// Optional: align path concatenation helper
static std::wstring JoinPathW(const std::wstring& base, const std::wstring& name) {
    if (base.empty())
        return name;
    if (base.back() == L'\\' || base.back() == L'/')
        return base + name;
    return base + L'\\' + name;
}

// ----------------- Drop-in scanDirectories implementation ------------------
QStringList GameListFrame::scanDirectories(const std::vector<std::filesystem::path>& baseDirs,
                                           int maxDepth, int currentDepth) {
    QStringList results;

    if (maxDepth < 1 || maxDepth > 3) {
        qWarning() << "Invalid scan depth:" << maxDepth << "(must be 1–3)";
        return results;
    }

    for (const auto& baseDir : baseDirs) {
        // Convert to wide path (UTF-16)
        std::wstring root = Utf8ToUtf16(baseDir.string());
        if (root.empty())
            continue;

        HANDLE dirH = CreateFileW(
            root.c_str(), FILE_LIST_DIRECTORY | SYNCHRONIZE, FILE_SHARE_ALL, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_SYNCHRONOUS_IO_NONALERT, nullptr);
        if (dirH == INVALID_HANDLE_VALUE) {
            // not accessible or doesn't exist
            continue;
        }

        BYTE buffer[64 * 1024]; // big buffer for fewer syscalls
        IO_STATUS_BLOCK ios{};
        bool restartScan = TRUE;

        for (;;) {
            NTSTATUS st = NtQueryDirectoryFile(dirH, nullptr, nullptr, nullptr, &ios, buffer,
                                               (ULONG)sizeof(buffer), FileDirectoryInformation,
                                               FALSE, nullptr, restartScan ? TRUE : FALSE);
            restartScan = FALSE;

            if (st == STATUS_NO_MORE_FILES)
                break;
            if (!NT_SUCCESS(st))
                break;

            BYTE* p = buffer;
            while (true) {
                FILE_DIRECTORY_INFORMATION* info = reinterpret_cast<FILE_DIRECTORY_INFORMATION*>(p);
                // filename is not null-terminated — create std::wstring from length
                std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));

                if (!(name == L"." || name == L"..")) {
                    std::wstring full = JoinPathW(root, name);

                    if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        // check param.sfo in that directory
                        if (HasParamSFO(full)) {
                            results << QString::fromWCharArray(full.c_str());
                        }
                        // recurse if allowed
                        if (currentDepth < maxDepth) {
                            // pass std::filesystem::path without UTF conversion (construct from
                            // wstring)
                            std::filesystem::path pth(full);
                            results << scanDirectories({pth}, maxDepth, currentDepth + 1);
                        }
                    } else if (name.size() > 4 &&
                               _wcsicmp(name.c_str() + (name.size() - 4), L".zar") == 0) {
                        const std::filesystem::path zar_path(full);
                        if (Core::FileSys::IsZArchiveFile(zar_path) &&
                            Core::FileSys::ReadGameFile(zar_path, "sce_sys/param.sfo")
                                .has_value()) {
                            results << QString::fromWCharArray(full.c_str());
                        }
                    }
                }

                if (info->NextEntryOffset == 0)
                    break;
                p += info->NextEntryOffset;
            }
        }

        CloseHandle(dirH);
    }

    return results;
}
#else
QStringList GameListFrame::scanDirectories(const std::vector<std::filesystem::path>& baseDirs,
                                           int maxDepth, int currentDepth) {
    QStringList results;

    // Only allow 1–3
    if (maxDepth < 1 || maxDepth > 3) {
        qWarning() << "Invalid scan depth:" << maxDepth << "(must be 1–3)";
        return results;
    }

    for (const auto& baseDir : baseDirs) {
        QDir dir(QString::fromStdString(baseDir.string()));
        if (!dir.exists())
            continue;

        QStringList entries = dir.entryList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        for (const QString& entry : entries) {
            QString fullPath = dir.absoluteFilePath(entry);
            QFileInfo entryInfo(fullPath);

            if (entryInfo.isDir()) {
                // Only include directories that have /sce_sys/param.sfo
                if (QFileInfo(fullPath + "/sce_sys/param.sfo").exists()) {
                    results << fullPath;
                }

                // Recurse if still below max depth
                if (currentDepth < maxDepth) {
                    results << scanDirectories({fullPath.toStdString()}, maxDepth,
                                               currentDepth + 1);
                }
            } else if (fullPath.endsWith(".zar", Qt::CaseInsensitive)) {
                const std::filesystem::path zar_path = fullPath.toStdString();
                if (Core::FileSys::IsZArchiveFile(zar_path) &&
                    Core::FileSys::ReadGameFile(zar_path, "sce_sys/param.sfo").has_value()) {
                    results << fullPath;
                }
            }
        }
    }

    return results;
}
#endif

void GameListFrame::PopulateFromCacheInstantly() {
    if (!m_info_cache) {
        return;
    }

    std::vector<GameInfo> cached = m_info_cache->GetAllForInstantList();
    if (cached.empty()) {
        return;
    }

    for (auto& info : cached) {
        auto game = std::make_shared<GUIGameInfo>();
        game->info = std::move(info);
        m_game_data.push_back(std::move(game));
    }

    Refresh(false, {}, false);
}

void GameListFrame::LoadPlayTimeData() {
    m_play_times.clear();

    const auto file_path = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "play_time.txt";
    std::ifstream in(file_path);
    if (!in) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string serial, time_str;
        qint64 last_played_unix = 0;
        if (!(iss >> serial >> time_str >> last_played_unix)) {
            continue;
        }

        int hours = 0, minutes = 0, seconds = 0;
        char c1 = 0, c2 = 0;
        std::istringstream ts(time_str);
        if (!(ts >> hours >> c1 >> minutes >> c2 >> seconds) || c1 != ':' || c2 != ':') {
            continue;
        }

        PlayTimeEntry entry;
        entry.seconds = static_cast<quint64>(hours) * 3600 + static_cast<quint64>(minutes) * 60 +
                        static_cast<quint64>(seconds);
        entry.last_played_unix = last_played_unix;
        m_play_times[serial] = entry;
    }
}

GameListFrame::PlayTimeEntry GameListFrame::GetPlayTimeEntry(const std::string& serial) const {
    const auto it = m_play_times.find(serial);
    return it != m_play_times.end() ? it->second : PlayTimeEntry{};
}

void GameListFrame::Refresh(const bool from_drive,
                            const std::vector<std::string>& serials_to_remove,
                            const bool scroll_after) {
    LoadPlayTimeData();

    if (from_drive) {
        WaitAndAbortSizeCalcThreads();
    }
    WaitAndAbortRepaintThreads();
    GUI::Utils::StopFutureWatcher(m_parsing_watcher, from_drive);
    GUI::Utils::StopFutureWatcher(m_refresh_watcher, from_drive);

    if (m_progress_dialog && m_progress_dialog->isVisible()) {
        m_progress_dialog->SetValue(m_progress_dialog->maximum());
        m_progress_dialog->accept();
    }

    if (from_drive) {
        m_path_entries.clear();
        m_path_list.clear();
        m_game_keys.clear();
        m_game_data.clear();
        m_notes.clear();
        m_titles.clear();
        m_games.pop_all();
        {
            std::lock_guard lock(m_pending_cache_puts_mutex);
            m_pending_cache_puts.clear();
        }

        if (!m_shown_instant_cache_list) {
            m_shown_instant_cache_list = true;
            PopulateFromCacheInstantly();
        }

        if (m_progress_dialog) {
            m_progress_dialog->SetValue(0);
        }
        std::vector<std::filesystem::path> game_dirs = m_emu_settings->GetGameInstallDirs();
        // Check if the list is empty
        if (game_dirs.empty()) {
            qWarning() << "Game directory list is empty, skipping refresh.";
            return;
        }

        // Check if at least one directory exists
        bool any_valid = false;
        for (const auto& dir : game_dirs) {
            if (std::filesystem::exists(dir)) {
                any_valid = true;
                break;
            }
        }
        if (!any_valid) {
            qWarning() << "No valid game directories found, skipping refresh.";
            return;
        }

        // Show progress dialog if available
        if (m_progress_dialog) {
            m_progress_dialog->show();
        }

        // Get directory scan depth from GUI settings
        int scan_depth = m_gui_settings->GetValue(GUI::general_directory_depth_scanning).toInt();

        m_parsing_watcher.setFuture(QtConcurrent::run([this, game_dirs, scan_depth]() {
            QStringList dirs =
                scanDirectories(game_dirs, scan_depth); // Make sure scanDirectories accepts vector

            std::vector<path_entry> new_entries;
            for (const QString& full_path : dirs) {
                if (m_parsing_watcher.isCanceled())
                    break;

                new_entries.emplace_back(path_entry{full_path.toStdString(), true});
            }

            if (!new_entries.empty()) {
                std::lock_guard lock(m_path_mutex);
                m_path_entries.insert(m_path_entries.end(), new_entries.begin(), new_entries.end());
            }
        }));
        return;
    }

    // Fill Game List / Game Grid

    const std::string selected_item = CurrentSelectionPath();

    // Release old data
    for (const auto& game : m_game_data) {
        game->item = nullptr;
    }

    // Get list of matching apps
    std::vector<game_info> matching_apps;

    for (const auto& app : m_game_data) {
        if (IsEntryVisible(app)) {
            matching_apps.push_back(app);
        }
    }

    // Fallback is not needed when at least one entry is visible
    if (matching_apps.empty()) {
        for (const auto& app : m_game_data) {
            if (IsEntryVisible(app, true)) {
                matching_apps.push_back(app);
            }
        }
    }

    if (m_is_list_layout) {
        m_game_grid->ClearList();
        const int scroll_position = m_game_list->verticalScrollBar()->value();
        m_game_list->Populate(matching_apps, m_notes, m_titles, selected_item);
        m_game_list->sort(m_game_data.size(), m_sort_column, m_col_sort_order);
        RepaintIcons();

        if (scroll_after) {
            m_game_list->scrollTo(m_game_list->currentIndex(), QAbstractItemView::PositionAtCenter);
        } else {
            m_game_list->verticalScrollBar()->setValue(scroll_position);
        }
    } else {
        m_game_list->ClearList();
        m_game_grid->Populate(matching_apps, m_notes, m_titles, selected_item);
        RepaintIcons();
    }

    Q_EMIT GameCountChanged(static_cast<int>(matching_apps.size()),
                            static_cast<int>(m_game_data.size()));
}

game_info GameListFrame::GetGameInfoByMode(const QTableWidgetItem* item) const {
    if (!item) {
        return nullptr;
    }

    if (m_is_list_layout) {
        return GetGameInfoFromItem(
            m_game_list->item(item->row(), static_cast<int>(GUI::GameListColumns::icon)));
    }

    return GetGameInfoFromItem(item);
}

game_info GameListFrame::GetGameInfoFromItem(const QTableWidgetItem* item) {
    if (!item) {
        return nullptr;
    }

    const QVariant var = item->data(GUI::game_role);
    if (!var.canConvert<game_info>()) {
        return nullptr;
    }

    return var.value<game_info>();
}

void GameListFrame::DoubleClickedSlot(QTableWidgetItem* item) {
    if (!item) {
        return;
    }

    DoubleClickedSlot(GetGameInfoByMode(item));
}

void GameListFrame::DoubleClickedSlot(const game_info& game) {
    if (!game) {
        return;
    }

    Q_EMIT RequestBoot(game);
}

void GameListFrame::OnCompatFinished() {
    for (const auto& game : m_game_data) {
        game->compat = m_game_compat->GetCompatibility(game->info.serial);
    }
    UpdateCompatColumn();
}

void GameListFrame::UpdateCompatColumn() {
    if (m_is_list_layout) {
        if (m_game_list) {
            m_game_list->UpdateCompatItems();
        }
    } else if (m_game_grid) {
        m_game_grid->update();
    }
}

bool GameListFrame::RemoveCustomConfiguration(const QString& serial, const game_info& game) {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                      (serial + ".json").toStdString();

    std::error_code ec;
    bool result = std::filesystem::remove(path, ec);

    if (result && game) {
        game->has_custom_config = false;
    } else if (ec && ec.value() != ENOENT) {
        result = false;
    }

    return result;
}

void GameListFrame::ShowContextMenu(const QPoint& pos) {
    QPoint global_pos;
    game_info gameinfo;

    if (m_is_list_layout) {
        QTableWidgetItem* item = m_game_list->item(m_game_list->indexAt(pos).row(),
                                                   static_cast<int>(GUI::GameListColumns::icon));
        global_pos = m_game_list->viewport()->mapToGlobal(pos);
        gameinfo = GetGameInfoFromItem(item);
    } else if (GameListGridItem* item =
                   static_cast<GameListGridItem*>(m_game_grid->SelectedItem())) {
        gameinfo = item->Game();
        global_pos = m_game_grid->mapToGlobal(pos);
    }

    if (!gameinfo) {
        return;
    }

    GameListContextMenu context_menu(this);
    context_menu.Show(gameinfo, global_pos);
}

void GameListFrame::PlayBackgroundMusic(game_info game) {
    // Don't start title music over a game that's already running.
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        return;
    }

    if (!m_gui_settings->GetValue(GUI::game_list_play_bg).toBool() ||
        game->info.snd0_path.empty()) {
        BackgroundMusicPlayer::getInstance().StopMusic();
        return;
    }

    BackgroundMusicPlayer::getInstance().PlayMusic(QString::fromStdString(game->info.snd0_path));
}

void GameListFrame::OnCompatUpdatedRequested() {
    m_game_compat->RequestCompatibility(true);
}

game_info GameListFrame::GetSelectedGameInfo() {
    game_info info;

    if (m_is_list_layout) {
        if (m_game_list->selectedItems().isEmpty())
            return nullptr;
        info = GetGameInfoFromItem(m_game_list->selectedItems().first());
    } else {
        if (!m_game_grid->SelectedItem())
            return nullptr;
        GameListGridItem* item = static_cast<GameListGridItem*>(m_game_grid->SelectedItem());
        info = item->Game();
    }

    return info;
}

void GameListFrame::PrintLog(QString entry, QColor textColor) {
    logDisplay->setTextColor(textColor);
    logDisplay->append(entry);
    QScrollBar* sb = logDisplay->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void GameListFrame::ShowLog(bool show) {
    if (show) {
        if (logDisplay->isHidden()) {
            logDisplay->show();
            splitter->setSizes({800, 200});
        }
    } else {
        if (!logDisplay->isHidden()) {
            logDisplay->hide();
        }
    }
}

void GameListFrame::requestShortcut(const GameInfo& currentInfo, QString emuPath) {
    // Path to shortcut/link
    QString linkPath;

    // Eboot path
    QString targetPath;
    Common::FS::PathToQString(targetPath, currentInfo.path);
    QString ebootPath = targetPath + "/eboot.bin";
    if (Core::FileSys::IsZArchiveFile(currentInfo.path)) {
        ebootPath = targetPath;
    }

    // Get the full path to the icon
    QString iconPath;
    Common::FS::PathToQString(iconPath, currentInfo.icon_path);
    QFileInfo iconFileInfo(iconPath);
    QString icoPath = iconFileInfo.absolutePath() + "/" + iconFileInfo.baseName() + ".ico";

    QString exePath;

#ifdef Q_OS_WIN
    linkPath =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" +
        QString::fromStdString(currentInfo.name).remove(QRegularExpression("[\\\\/:*?\"<>|]")) +
        ".lnk";

    exePath = QCoreApplication::applicationFilePath().replace("\\", "/");
#else
    linkPath =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" +
        QString::fromStdString(currentInfo.name).remove(QRegularExpression("[\\\\/:*?\"<>|]")) +
        ".desktop";
#endif
    // Convert the icon to .ico if necessary
    if (iconFileInfo.suffix().toLower() == "png") {
        // Convert icon from PNG to ICO
        if (convertPngToIco(iconPath, icoPath)) {

#ifdef Q_OS_WIN
            if (createShortcutWin(linkPath, ebootPath, icoPath, exePath, emuPath)) {
#else
            if (createShortcutLinux(linkPath, currentInfo.name, ebootPath, iconPath, emuPath)) {
#endif
                QMessageBox::information(
                    nullptr, tr("Shortcut creation"),
                    QString(tr("Shortcut created successfully!") + "\n%1").arg(linkPath));
            } else {
                QMessageBox::critical(
                    nullptr, tr("Error"),
                    QString(tr("Error creating shortcut!") + "\n%1").arg(linkPath));
            }
        } else {
            QMessageBox::critical(nullptr, tr("Error"), tr("Failed to convert icon."));
        }

        // If the icon is already in ICO format, we just create the shortcut
    } else {
#ifdef Q_OS_WIN
        if (createShortcutWin(linkPath, ebootPath, iconPath, exePath, emuPath)) {
#else
        if (createShortcutLinux(linkPath, currentInfo.name, ebootPath, iconPath, emuPath)) {
#endif
            QMessageBox::information(
                nullptr, tr("Shortcut creation"),
                QString(tr("Shortcut created successfully!") + "\n%1").arg(linkPath));
        } else {
            QMessageBox::critical(nullptr, tr("Error"),
                                  QString(tr("Error creating shortcut!") + "\n%1").arg(linkPath));
        }
    }
}

#ifdef _WIN32
bool GameListFrame::createShortcutWin(const QString& linkPath, const QString& targetPath,
                                      const QString& iconPath, const QString& exePath,
                                      QString emuPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Create the ShellLink object
    Microsoft::WRL::ComPtr<IShellLink> pShellLink;
    HRESULT hres =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pShellLink));
    if (SUCCEEDED(hres)) {
        // Defines the path to the program executable
        pShellLink->SetPath((LPCWSTR)exePath.utf16());

        // Sets the home directory ("Start in")
        pShellLink->SetWorkingDirectory((LPCWSTR)QFileInfo(exePath).absolutePath().utf16());

        // Set arguments, eboot.bin file location

        QString arguments;

        if (emuPath == "") {
            arguments = QString("-d -g \"%1\"").arg(targetPath);
        } else {
            arguments = QString("-e \"%1\" -g \"%2\"").arg(emuPath, targetPath);
        }
        pShellLink->SetArguments((LPCWSTR)arguments.utf16());

        // Set the icon for the shortcut
        pShellLink->SetIconLocation((LPCWSTR)iconPath.utf16(), 0);

        // Save the shortcut
        Microsoft::WRL::ComPtr<IPersistFile> pPersistFile;
        hres = pShellLink.As(&pPersistFile);
        if (SUCCEEDED(hres)) {
            hres = pPersistFile->Save((LPCWSTR)linkPath.utf16(), TRUE);
        }
    }

    CoUninitialize();

    return SUCCEEDED(hres);
}
#else
bool GameListFrame::createShortcutLinux(const QString& linkPath, const std::string& name,
                                        const QString& targetPath, const QString& iconPath,
                                        QString emuPath) {
    QFile file(linkPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QString arguments;
    if (emuPath.isEmpty())
        arguments = QString("-d -g \"%1\"").arg(targetPath);
    else
        arguments = QString("-e \"%1\" -g \"%2\"").arg(emuPath, targetPath);

    const QString exePath = QCoreApplication::applicationFilePath();

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=" << QString::fromStdString(name) << "\n";
    out << "Exec=" << exePath << " " << arguments << "\n";
    out << "Icon=" << iconPath << "\n";
    out << "Categories=Game;\n";
    file.close();

    // Mark the .desktop file executable
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup);
    return true;
}
#endif

bool GameListFrame::convertPngToIco(const QString& pngFilePath, const QString& icoFilePath) {
    // Load the PNG image
    QImage image(pngFilePath);
    if (image.isNull()) {
        return false;
    }

    // Scale the image to the default icon size (256x256 pixels)
    QImage scaledImage =
        image.scaled(QSize(256, 256), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Convert the image to QPixmap
    QPixmap pixmap = QPixmap::fromImage(scaledImage);

    // Save the pixmap as an ICO file
    if (pixmap.save(icoFilePath, "ICO")) {
        return true;
    } else {
        return false;
    }
}
