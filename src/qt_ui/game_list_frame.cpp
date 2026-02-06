// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <regex>
#include <set>
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
#include <QPushButton>
#include <QScrollBar>
#include <QtConcurrent>
#include <fmt/core.h>
#include "background_music_player.h"
#include "change_log_dialog.h"
#include "common/singleton.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/ipc/ipc_client.h"
#include "game_list_frame.h"
#include "game_list_grid.h"
#include "game_list_grid_item.h"
#include "game_list_table.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "localized.h"
#include "npbind_dialog.h"
#include "persistent_settings.h"
#include "progress_dialog.h"
#include "qt_utils.h"

#ifdef _WIN32
#include <string>
#include <vector>
#include <QDebug>
#include <QStringList>
#include <windows.h>
#include <winternl.h>
#endif
#include <common/log_analyzer.h>
#include <common/path_util.h>
#include "cheats_patches_dialog.h"
#include "core/ipc/ipc_client.h"
#include "settings_dialog.h"
#include "sfo_viewer_dialog.h"

GameListFrame::GameListFrame(std::shared_ptr<GUISettings> gui_settings,
                             std::shared_ptr<EmulatorSettings> emu_settings,
                             std::shared_ptr<PersistentSettings> persistent_settings,
                             std::shared_ptr<IpcClient> ipc_client, QWidget* parent)
    : CustomDockWidget(tr("Game List"), parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emu_settings)), m_ipc_client(std::move(ipc_client)),
      m_persistent_settings(std::move(persistent_settings)) {

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

    m_game_list = new GameListTable(this, m_gui_settings, m_persistent_settings);
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

    splitter = new QSplitter(Qt::Vertical);
    logDisplay = new QTextEdit(splitter);

    QPalette logPalette = logDisplay->palette();
    logPalette.setColor(QPalette::Base, Qt::black);
    logPalette.setColor(QPalette::Text, Qt::white);
    logDisplay->setPalette(logPalette);
    logDisplay->setText(tr("Game Log"));
    logDisplay->setReadOnly(true);

    splitter->addWidget(m_central_widget);
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
        m_serials.clear();
        m_game_data.clear();
        m_notes.clear();
        m_games.pop_all();
    });

    connect(&m_parsing_watcher, &QFutureWatcher<void>::finished, this,
            &GameListFrame::OnParsingFinished);
    connect(&m_parsing_watcher, &QFutureWatcher<void>::canceled, this, [this]() {
        WaitAndAbortSizeCalcThreads();
        WaitAndAbortRepaintThreads();

        m_path_entries.clear();
        m_path_list.clear();
        m_game_data.clear();
        m_serials.clear();
        m_games.pop_all();
    });
    connect(&m_refresh_watcher, &QFutureWatcher<void>::finished, this,
            &GameListFrame::OnRefreshFinished);
    connect(&m_refresh_watcher, &QFutureWatcher<void>::canceled, this, [this]() {
        WaitAndAbortSizeCalcThreads();
        WaitAndAbortRepaintThreads();

        m_path_entries.clear();
        m_path_list.clear();
        m_game_data.clear();
        m_serials.clear();
        m_games.pop_all();

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
        Refresh();
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

bool GameListFrame::SearchMatchesApp(const QString& name, const QString& serial,
                                     bool fallback) const {
    if (!m_search_text.isEmpty()) {
        QString search_text = m_search_text.toLower();
        QString title_name;

        if (const auto it = m_titles.find(serial); it != m_titles.cend()) {
            title_name = it->second.toLower();
        } else {
            title_name = name.toLower();
        }

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

        return title_name.contains(search_text) || serial.toLower().contains(search_text);
    }
    return true;
}

bool GameListFrame::IsEntryVisible(const game_info& game, bool search_fallback) const {
    const QString serial = QString::fromStdString(game->info.serial);
    const bool is_visible = m_show_hidden || !m_hidden_list.contains(serial);
    return is_visible &&
           SearchMatchesApp(QString::fromStdString(game->info.name), serial, search_fallback);
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

    const auto add_game = [this, localized_title, localized_icon](const std::string& dir_or_elf) {
        GUIGameInfo game{};
        game.info.path = GUI::Utils::NormalizePath(std::filesystem::path(dir_or_elf));

        const Localized thread_localized;

        const std::string sfo_dir = dir_or_elf + "/sce_sys";
        PSF psf;
        psf.Open(sfo_dir + "/param.sfo");
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
        if (m_npfile.Load(dir_or_elf + "/sce_sys/npbind.dat")) {
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

            if (const auto pubtool_info = psf.GetString("PUBTOOLINFO"); pubtool_info.has_value()) {
                u64 sdk_ver_offset = pubtool_info.value().find("sdk_ver");
                if (sdk_ver_offset == pubtool_info.value().npos) {
                    game.info.sdk_ver = "0.00";
                } else {
                    // Increment offset to account for sdk_ver= part of string.
                    sdk_ver_offset += 8;
                    u64 sdk_ver_len = pubtool_info.value().find(",", sdk_ver_offset);
                    if (sdk_ver_len == pubtool_info.value().npos) {
                        // If there's no more commas, this is likely the last entry of pubtool info.
                        // Use string length instead.
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

            if (const auto save_dir = psf.GetString("INSTALL_DIR_SAVEDATA"); save_dir.has_value()) {
                game.info.save_dir = *save_dir;
            } else {
                game.info.save_dir = game.info.serial;
            }
        }

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

        const QString serial = QString::fromStdString(game.info.serial);

        m_games_mutex.lock();

        // Read persistent_settings values
        const QString last_played =
            m_persistent_settings->GetValue(GUI::Persistent::last_played, serial, "").toString();
        const quint64 playtime =
            m_persistent_settings->GetValue(GUI::Persistent::playtime, serial, 0).toULongLong();

        // Set persistent_settings values if values exist
        if (!last_played.isEmpty()) {
            m_persistent_settings->SetLastPlayed(
                serial, last_played,
                false); // No need to sync here. It would slow down the refresh anyway.
        }
        if (playtime > 0) {
            m_persistent_settings->SetPlaytime(
                serial, playtime,
                false); // No need to sync here. It would slow down the refresh anyway.
        }

        m_serials.insert(serial);

        if (QString note =
                m_persistent_settings->GetValue(GUI::Persistent::notes, serial, "").toString();
            !note.isEmpty()) {
            m_notes.insert_or_assign(serial, std::move(note));
        }

        if (QString title = m_persistent_settings->GetValue(GUI::Persistent::titles, serial, "")
                                .toString()
                                .simplified();
            !title.isEmpty()) {
            m_titles.insert_or_assign(serial, std::move(title));
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

            // if (entry.is_from_file) { //TODO
            if (std::filesystem::is_regular_file(entry.path + "/sce_sys/param.sfo")) {
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

    // Move parsed results into main game data list
    for (auto&& g : m_games.pop_all()) {
        m_game_data.push_back(g);
    }

    const s32 language_index = GUIApplication::getLanguageId();
    const std::string localized_icon = fmt::format("ICON0_%02d.PNG", language_index);

    std::vector<game_info> filtered_games;
    filtered_games.reserve(m_game_data.size());

    // Merge base and update game info (CUSAxxxxx + CUSAxxxxx-UPDATE) or -patch
    for (const game_info& entry : m_game_data) {
        // Skip update folders (we’ll merge them into base)
        if (entry->info.path.ends_with("-UPDATE") || entry->info.path.ends_with("-patch"))
            continue;

        for (const auto& other : m_game_data) {
            // Process only matching update or patch folders
            if (!other->info.path.ends_with("-UPDATE") && !other->info.path.ends_with("-patch"))
                continue;

            auto starts_with = [](const std::string& str, const std::string& prefix) {
                return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
            };

            const std::string base_path = entry->info.path;
            const std::string other_path = other->info.path;

            // Match by serial and full path prefix (including "-UPDATE" -patch folders)
            if (entry->info.serial != other->info.serial ||
                !(starts_with(other_path, base_path + "-UPDATE") ||
                  starts_with(other_path, base_path + "-patch"))) {
                continue;
            }

            // --- Directly override with update data ---
            entry->info.app_ver = other->info.app_ver;
            entry->info.fw = other->info.fw;
            entry->info.sdk_ver = other->info.sdk_ver;
            entry->info.np_comm_ids = other->info.np_comm_ids;

            entry->info.update_path = other->info.path; // Store update path

            // --- Replace picture path if available ---
            if (std::string pic_path = other->info.path + "/sce_sys/PIC1.PNG";
                std::filesystem::is_regular_file(pic_path))
                entry->info.pic_path = std::move(pic_path);

            // --- Replace icon path if available ---
            if (std::string icon_path = other->info.path + "/sce_sys/" + localized_icon;
                std::filesystem::is_regular_file(icon_path))
                entry->info.icon_path = std::move(icon_path);
            else if (std::string icon_path = other->info.path + "/sce_sys/ICON0.PNG";
                     std::filesystem::is_regular_file(icon_path))
                entry->info.icon_path = std::move(icon_path);

            // --- Replace sound path if available ---
            if (std::string snd0_path = other->info.path + "/sce_sys/snd0.at9";
                std::filesystem::is_regular_file(snd0_path))
                entry->info.snd0_path = std::move(snd0_path);
        }

        // Keep only base games (hide -update folders)
        filtered_games.push_back(entry);
    }

    // Replace with filtered list (no -update entries)
    m_game_data.swap(filtered_games);

    // Sort alphabetically by title (localized if available)
    std::sort(m_game_data.begin(), m_game_data.end(),
              [&](const game_info& game1, const game_info& game2) {
                  const QString serial1 = QString::fromStdString(game1->info.serial);
                  const QString serial2 = QString::fromStdString(game2->info.serial);
                  const QString& title1 = m_titles.contains(serial1)
                                              ? m_titles.at(serial1)
                                              : QString::fromStdString(game1->info.name);
                  const QString& title2 = m_titles.contains(serial2)
                                              ? m_titles.at(serial2)
                                              : QString::fromStdString(game2->info.name);
                  return title1.toLower() < title2.toLower();
              });

    // Clean up hidden games list
    m_hidden_list.intersect(m_serials);
    m_gui_settings->SetValue(GUI::game_list_hidden_list, QStringList(m_hidden_list.values()));
    m_serials.clear();
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

        QStringList entries =
            dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        for (const QString& entry : entries) {
            QString fullPath = dir.absoluteFilePath(entry);

            // Only include directories that have /sce_sys/param.sfo
            if (QFileInfo(fullPath + "/sce_sys/param.sfo").exists()) {
                results << fullPath;
            }

            // Recurse if still below max depth
            if (currentDepth < maxDepth) {
                results << scanDirectories({fullPath.toStdString()}, maxDepth, currentDepth + 1);
            }
        }
    }

    return results;
}
#endif

void GameListFrame::Refresh(const bool from_drive,
                            const std::vector<std::string>& serials_to_remove,
                            const bool scroll_after) {
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
        m_serials.clear();
        m_game_data.clear();
        m_notes.clear();
        m_games.pop_all();

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
    Refresh();
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

    auto deleteHandler = [this, gameinfo](DeleteType type) {
        bool error = false;
        QString folder_path;
        QString message_type;

        QString game_path;
        Common::FS::PathToQString(game_path, gameinfo->info.path);

        QString update_path = game_path + "-UPDATE";
        if (!std::filesystem::exists(Common::FS::PathFromQString(update_path))) {
            update_path = game_path + "-patch";
        }

        QString dlc_path;
        Common::FS::PathToQString(
            dlc_path, m_emu_settings->GetAddonInstallDir() /
                          Common::FS::PathFromQString(game_path).parent_path().filename());

        // TODO: Replace 1 with the user's number
        QString save_data_path;
        Common::FS::PathToQString(save_data_path,
                                  Common::FS::GetUserPath(Common::FS::PathType::UserDir) /
                                      "savedata" / "1" / gameinfo->info.serial);

        // QString trophy_path;
        // Common::FS::PathToQString(trophy_path,
        //                           Common::FS::GetUserPath(Common::FS::PathType::MetaDataDir) /
        //                               gameinfo->info.serial / "TrophyFiles");

        switch (type) {
        case DeleteType::Game:
            BackgroundMusicPlayer::getInstance().StopMusic();
            folder_path = game_path;
            message_type = tr("Game");
            break;

        case DeleteType::Update:
            if (!std::filesystem::exists(Common::FS::PathFromQString(update_path))) {
                QMessageBox::critical(this, tr("Error"), tr("This game has no update to delete!"));
                return;
            }
            folder_path = update_path;
            message_type = tr("Update");
            break;

        case DeleteType::DLC:
            if (!std::filesystem::exists(Common::FS::PathFromQString(dlc_path))) {
                QMessageBox::critical(this, tr("Error"), tr("This game has no DLC to delete!"));
                return;
            }
            folder_path = dlc_path;
            message_type = tr("DLC");
            break;

        case DeleteType::SaveData:
            if (!std::filesystem::exists(Common::FS::PathFromQString(save_data_path))) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("This game has no save data to delete!"));
                return;
            }
            folder_path = save_data_path;
            message_type = tr("Save Data");
            break;

        case DeleteType::Trophy:
            //     if (!std::filesystem::exists(Common::FS::PathFromQString(trophy_path))) {
            //         QMessageBox::critical(this, tr("Error"),
            //                               tr("This game has no saved trophies to delete!"));
            //         return;
            //     }
            //     folder_path = trophy_path;
            //     message_type = tr("Trophy");
            break;

        case DeleteType::ShaderCache: {
            QString shader_cache_path;
            QString shader_cache_zip;

            Common::FS::PathToQString(shader_cache_path,
                                      Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
                                          gameinfo->info.serial);

            Common::FS::PathToQString(shader_cache_zip,
                                      Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
                                          (gameinfo->info.serial + ".zip"));

            const auto dir_path = Common::FS::PathFromQString(shader_cache_path);
            const auto zip_path = Common::FS::PathFromQString(shader_cache_zip);

            bool has_dir = std::filesystem::exists(dir_path);
            bool has_zip = std::filesystem::exists(zip_path);

            if (!has_dir && !has_zip) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("This game has no Shader Cache to delete!"));
                return;
            }

            if (has_dir)
                std::filesystem::remove_all(dir_path);
            if (has_zip)
                std::filesystem::remove(zip_path);

            QMessageBox::information(this, tr("Shader Cache"),
                                     tr("Shader cache deleted successfully."));
            return;
        }
        }

        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("Delete %1").arg(message_type),
            tr("Are you sure you want to delete %1's %2 directory?")
                .arg(QString::fromStdString(gameinfo->info.name), message_type),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            QDir(folder_path).removeRecursively();

            if (type == DeleteType::Game) {
                Refresh(true);
            }
        }
    };

    GameInfo current_game = gameinfo->info;
    const QString serial = QString::fromStdString(current_game.serial);
    const QString name = QString::fromStdString(current_game.name).simplified();

    QMenu menu;

    QAction* configure = menu.addAction(
        gameinfo->has_custom_config ? tr("&Change Custom Configuration")
                                    : tr("&Create Custom Configuration From Global Settings"));

    // this will work only for separate updates install (-UPDATE or -patch folders)
    const std::string update_path = current_game.update_path;
    if (!update_path.empty()) {
        QAction* change_log = menu.addAction(tr("&View Changelog"));
        // changelog
        connect(change_log, &QAction::triggered, this, [this, update_path] {
            const s32 language_index = GUIApplication::getLanguageId();
            const std::string localized_changelog =
                fmt::format("changeinfo_%02d.xml", language_index);
            std::string changelog_path = update_path + "/sce_sys/changeinfo/" + localized_changelog;
            if (!std::filesystem::is_regular_file(changelog_path)) {
                changelog_path = update_path + "/sce_sys/changeinfo/changeinfo.xml";
                if (std::filesystem::is_regular_file(changelog_path)) {
                    ChangelogDialog dialog(this, QString::fromStdString(changelog_path));
                    dialog.exec();
                }
            }
        });
    } else {
        // if changelog is in base folder (merged updates)
        const s32 language_index = GUIApplication::getLanguageId();
        const std::string localized_changelog = fmt::format("changeinfo_%02d.xml", language_index);
        std::string changelog_path =
            current_game.path + "/sce_sys/changeinfo/" + localized_changelog;
        if (!std::filesystem::is_regular_file(changelog_path)) {
            changelog_path = current_game.path + "/sce_sys/changeinfo/changeinfo.xml";
            if (std::filesystem::is_regular_file(changelog_path)) {
                QAction* change_log = menu.addAction(tr("&View Changelog"));
                connect(change_log, &QAction::triggered, this, [this, changelog_path] {
                    ChangelogDialog dialog(this, QString::fromStdString(changelog_path));
                    dialog.exec();
                });
            }
        }
    }
    // Open Menu
    QMenu* open_menu = menu.addMenu(tr("&Open Folder"));
    QAction* open_game_path = open_menu->addAction(tr("&Open Game Folder"));
    connect(open_game_path, &QAction::triggered, this, [current_game] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(current_game.path)));
    });
    if (!update_path.empty()) {
        QAction* open_update_path = open_menu->addAction(tr("&Open Update Folder"));
        connect(open_update_path, &QAction::triggered, this, [update_path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(update_path)));
        });
    }
    QAction* open_log_folder = open_menu->addAction(tr("&Open Log Folder"));
    connect(open_log_folder, &QAction::triggered, this, [this, serial] {
        // Get log folder path
        QString logPath;
        Common::FS::PathToQString(logPath, Common::FS::GetUserPath(Common::FS::PathType::LogDir));

        if (!m_emu_settings->IsSeparateLoggingEnabled()) {
            // Open the entire log folder
            QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
            return;
        }

        // Construct per-game log file path
        QString fileName = serial + ".log";
        QString filePath = QDir(logPath).filePath(fileName);

        if (QFile::exists(filePath)) {
#ifdef Q_OS_WIN
            QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(filePath)});
#elif defined(Q_OS_MAC)
        QProcess::startDetached("open", {"-R", filePath});
#elif defined(Q_OS_LINUX)
        // Try common Linux file managers
        bool opened = QProcess::startDetached("nautilus", {"--select", filePath});
        if (!opened) opened = QProcess::startDetached("xdg-open", {logPath});
        if (!opened) opened = QProcess::startDetached("dolphin", {"--select", filePath});
        if (!opened) opened = QProcess::startDetached("thunar", {"--select", filePath});
        if (!opened) {
            // Last fallback
            QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
        }
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
#endif
        } else {
            // Log file does not exist: show info message
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setWindowTitle(tr("Log Not Found"));
            msgBox.setText(tr("No log file found for this game!"));

            QPushButton* okButton = msgBox.addButton(QMessageBox::Ok);
            QPushButton* openFolderButton =
                msgBox.addButton(tr("Open Log Folder"), QMessageBox::ActionRole);

            msgBox.exec();

            if (msgBox.clickedButton() == openFolderButton) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
            }
        }
    });
    // SFO viewer
    QAction* sfo_view = menu.addAction(tr("&SFO viewer"));
    connect(sfo_view, &QAction::triggered, this, [this, current_game] {
        QString sfo_path;
        if (!current_game.update_path.empty()) {
            sfo_path = QString::fromStdString(current_game.update_path) + "/sce_sys/param.sfo";
        } else {
            sfo_path = QString::fromStdString(current_game.path) + "/sce_sys/param.sfo";
        }
        SFOViewerDialog dialog(this, sfo_path);
        dialog.exec();
    });

    QAction* npbind_view = menu.addAction(tr("&npbind.dat viewer"));
    connect(npbind_view, &QAction::triggered, this, [this, current_game] {
        QString npbind_path;
        if (!current_game.update_path.empty()) {
            npbind_path = QString::fromStdString(current_game.update_path) + "/sce_sys/npbind.dat";
        } else {
            npbind_path = QString::fromStdString(current_game.path) + "/sce_sys/npbind.dat";
        }
        NpBindDialog dialog(this, npbind_path);
        dialog.exec();
    });

    QAction* cheats_view = menu.addAction(tr("&Cheats & Patches"));
    connect(cheats_view, &QAction::triggered, this, [this, current_game] {
        QString gameName = QString::fromStdString(current_game.name);
        QString gameSerial = QString::fromStdString(current_game.serial);
        QString gameVersion = QString::fromStdString(current_game.app_ver);
        QString gameSize = GUI::Utils::FormatByteSize(current_game.size_on_disk);
        QString iconPath;
        Common::FS::PathToQString(iconPath, current_game.icon_path);
        QPixmap gameImage(iconPath);
        CheatsPatches* cheatsPatches = new CheatsPatches(
            m_gui_settings, m_ipc_client, gameName, gameSerial, gameVersion, gameSize, gameImage);
        cheatsPatches->show();
        connect(this, &QWidget::destroyed, cheatsPatches,
                [cheatsPatches]() { cheatsPatches->deleteLater(); });
    });

    QMenu* trophy_viewer = menu.addMenu(tr("&Trophy Viewer"));
    const auto valid_users = m_emu_settings->GetUserManager().GetValidUsers();
    for (const auto& user : valid_users) {
        QString user_label =
            QString("%1 (ID: %2)").arg(QString::fromStdString(user.user_name)).arg(user.user_id);
        QAction* user_action = trophy_viewer->addAction(user_label);
        connect(user_action, &QAction::triggered, this, [this, user, current_game] {

        });
    }

    // Manage Game Menu
    QMenu* manage_game_menu = menu.addMenu(tr("&Manage Game"));

    QAction* hide_serial = manage_game_menu->addAction(tr("&Hide From Game List"));
    hide_serial->setCheckable(true);
    hide_serial->setChecked(m_hidden_list.contains(serial));
    QAction* edit_notes = manage_game_menu->addAction(tr("&Add/Edit Tooltip Notes"));

    // Copy Info menu
    QMenu* info_menu = menu.addMenu(tr("&Copy Info"));
    QAction* copy_info = info_menu->addAction(tr("&Copy Name + Serial"));
    QAction* copy_name = info_menu->addAction(tr("&Copy Name"));
    QAction* copy_serial = info_menu->addAction(tr("&Copy Serial"));

    // Delete
    QMenu* delete_menu = menu.addMenu(tr("&Delete..."));
    QAction* delete_game = delete_menu->addAction(tr("&Delete Game"));
    QAction* delete_update = delete_menu->addAction(tr("&Delete Update"));
    QAction* delete_save_data = delete_menu->addAction(tr("&Delete Save Data"));
    QAction* delete_DLC = delete_menu->addAction(tr("&Delete DLC "));
    QAction* delete_trophy = delete_menu->addAction(tr("&Delete Trophy"));
    QAction* delete_shader_cache = delete_menu->addAction(tr("&Delete Shader Cache"));
    delete_trophy->setEnabled(false); // TODO

    // Compatibility
    QMenu* compatibility_menu = menu.addMenu(tr("&Compatibility"));
    QAction* compatibility_view = compatibility_menu->addAction(tr("&View Report"));
    QAction* compatibility_submit = compatibility_menu->addAction(tr("&Submit Report"));
    QAction* compatibility_update = compatibility_menu->addAction(tr("&Update Database"));

    compatibility_view->setEnabled(gameinfo->compat.index <=
                                   4); // enable for status Playable to Nothing

    // Copy Menu Actions
    connect(copy_info, &QAction::triggered, this, [name, serial] {
        QApplication::clipboard()->setText(name % QStringLiteral(" [") % serial %
                                           QStringLiteral("]"));
    });
    connect(copy_name, &QAction::triggered, this,
            [name] { QApplication::clipboard()->setText(name); });
    connect(copy_serial, &QAction::triggered, this,
            [serial] { QApplication::clipboard()->setText(serial); });

    // Delete Menu Actions
    connect(delete_game, &QAction::triggered, this, [=] { deleteHandler(DeleteType::Game); });
    connect(delete_update, &QAction::triggered, this, [=] { deleteHandler(DeleteType::Update); });
    connect(delete_save_data, &QAction::triggered, this,
            [=] { deleteHandler(DeleteType::SaveData); });
    connect(delete_DLC, &QAction::triggered, this, [=] { deleteHandler(DeleteType::DLC); });
    connect(delete_trophy, &QAction::triggered, this, [=] { deleteHandler(DeleteType::Trophy); });
    connect(delete_shader_cache, &QAction::triggered, this,
            [=] { deleteHandler(DeleteType::ShaderCache); });

    // Compatibility menu actions
    connect(compatibility_view, &QAction::triggered, this, [this, gameinfo] {
        if (gameinfo->compat.issue_number != "") {
            QDesktopServices::openUrl(
                QUrl(m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
                     "issues/" + gameinfo->compat.issue_number));
        } else {
            QMessageBox::information(
                this, tr("No Report Available"),
                tr("There is no compatibility report available for this game."));
        }
    });
    connect(compatibility_submit, &QAction::triggered, this, [this, current_game, gameinfo] {
        std::filesystem::path log_file_path =
            (Common::FS::GetUserPath(Common::FS::PathType::LogDir) /
             (m_emu_settings->IsSeparateLoggingEnabled() ? current_game.serial + ".log"
                                                         : "shad_log.txt"));
        bool is_valid_file = LogAnalyzer::ProcessFile(log_file_path);
        std::optional<std::string> report_result = std::nullopt;
        if (is_valid_file) {
            report_result = LogAnalyzer::CheckResults(current_game.serial);
        }
        if ((!is_valid_file || report_result.has_value()) &&
            !m_gui_settings->GetValue(GUI::compatibility_bypass_loganalyzer).toBool()) {
            QString error_string;
            if (report_result.has_value()) {
                error_string = QString::fromStdString(*report_result);
            } else {
                error_string =
                    tr("The log is invalid, it either doesn't exist or log filters were used.");
            }
            QMessageBox msgBox(QMessageBox::Critical, tr("Error"),
                               tr("Couldn't submit report, because the latest log for the "
                                  "game failed on the following check, and therefore would be "
                                  "an invalid report:") +
                                   "\n" + error_string);
            auto okButton = msgBox.addButton(tr("Ok"), QMessageBox::AcceptRole);
            auto infoButton = msgBox.addButton(tr("Info"), QMessageBox::ActionRole);
            msgBox.setEscapeButton(okButton);
            msgBox.exec();
            if (msgBox.clickedButton() == infoButton) {
                QDesktopServices::openUrl(
                    QUrl(m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
                         "?tab=readme-ov-file#rules"));
            }
            return;
        }
        if (gameinfo->compat.issue_number == "") {
            QUrl url = QUrl(m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
                            "issues/new");
            QUrlQuery query;
            query.addQueryItem("template", QString("game_compatibility.yml"));
            query.addQueryItem("title",
                               QString("%1 - %2").arg(QString::fromStdString(current_game.serial),
                                                      QString::fromStdString(current_game.name)));
            query.addQueryItem("game-name", QString::fromStdString(current_game.name));
            query.addQueryItem("game-serial", QString::fromStdString(current_game.serial));
            query.addQueryItem("game-version", QString::fromStdString(current_game.app_ver));
            query.addQueryItem("emulator-version",
                               QString::fromStdString(*LogAnalyzer::entries[1]->GetParsedData()));
            url.setQuery(query);

            QDesktopServices::openUrl(url);
        } else {
            auto url_issues =
                m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() + "issues/";
            QDesktopServices::openUrl(QUrl(url_issues + gameinfo->compat.issue_number));
        }
    });
    connect(compatibility_update, &QAction::triggered, this,
            [this] { m_game_compat->RequestCompatibility(true); });

    // Manage Game menu actions
    connect(hide_serial, &QAction::triggered, this, [serial, this](bool checked) {
        if (checked)
            m_hidden_list.insert(serial);
        else
            m_hidden_list.remove(serial);

        m_gui_settings->SetValue(GUI::game_list_hidden_list, QStringList(m_hidden_list.values()));
        Refresh();
    });
    connect(edit_notes, &QAction::triggered, this, [this, name, serial] {
        bool accepted;
        // fetch old notes from persistent storage
        const QString old_notes =
            m_persistent_settings->GetValue(GUI::Persistent::notes, serial, "").toString();

        QInputDialog dlg(this);
        dlg.setWindowTitle(tr("Edit Tooltip Notes"));
        dlg.setLabelText(name + "\n" + serial);
        dlg.setOption(QInputDialog::UsePlainTextEditForTextInput, true);
        dlg.setTextValue(old_notes);
        dlg.setMinimumSize(300, 200);

        if (dlg.exec() == QDialog::Accepted) {
            const QString new_notes = dlg.textValue().trimmed();

            if (new_notes.isEmpty()) {
                m_notes.erase(serial);
                m_persistent_settings->RemoveValue(GUI::Persistent::notes, serial);
            } else {
                m_notes.insert_or_assign(serial, new_notes);
                m_persistent_settings->SetValue(GUI::Persistent::notes, serial, new_notes);
            }

            Refresh();
        }
    });
    auto configure_dialog = [this, current_game, gameinfo](bool create_cfg_from_global_cfg) {
        SettingsDialog dlg(m_gui_settings, m_emu_settings, m_ipc_client, 0, this, &current_game,
                           create_cfg_from_global_cfg);

        /*connect(&dlg, &settings_dialog::EmuSettingsApplied, [this, gameinfo]() {
            if (!gameinfo->has_custom_config) {
                gameinfo->has_custom_config = true;
                m_game_list_frame->ShowCustomConfigIcon(gameinfo);
            }
            Q_EMIT m_game_list_frame->NotifyEmuSettingsChange();
        });*/

        dlg.exec();
    };
    connect(configure, &QAction::triggered, this,
            [configure_dialog = std::move(configure_dialog)]() { configure_dialog(true); });

    menu.exec(global_pos);
}

void GameListFrame::PlayBackgroundMusic(game_info game) {
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
