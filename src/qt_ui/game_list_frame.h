// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/lf_queue.h"
#include "custom_dock_widget.h"
#include "game_list.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>

#include <filesystem>
#include <memory>
#include <optional>
#include <set>

class GameListTable;
class GameListGrid;
class GUISettings;
class EmulatorSettings;
class PersistentSettings;
class ProgressDialog;
class IpcClient;

class GameListFrame : public CustomDockWidget {
    Q_OBJECT

public:
    explicit GameListFrame(std::shared_ptr<GUISettings> gui_settings,
                           std::shared_ptr<EmulatorSettings> emu_settings,
                           std::shared_ptr<PersistentSettings> persistent_settings,
                           std::shared_ptr<IpcClient> ipc_client, QWidget* parent = nullptr);
    ~GameListFrame();

    /** Refresh the gamelist with/without loading game data from files. Public so that main frame
     * can refresh after install */
    void Refresh(const bool from_drive = false,
                 const std::vector<std::string>& serials_to_remove = {},
                 const bool scroll_after = true);
    /** Loads from settings. Public so that main frame can easily reset these settings if needed. */
    void LoadSettings();
    /** Saves settings. Public so that main frame can save this when a caching of column widths is
     * needed for settings backup */
    void SaveSettings();
    /** Repaint Gamelist Icons with new background color */
    void RepaintIcons(const bool& from_settings = false);
    /** Resize Gamelist Icons to size given by slider position */
    void ResizeIcons(const int& slider_pos);
    void SetShowHidden(bool show);
    bool IsEntryVisible(const game_info& game, bool search_fallback = false) const;
    const std::vector<game_info>& GetGameInfo() const;
    void CheckCompatibilityAtStartup();
    void PlayBackgroundMusic(game_info game);

    QImage backgroundImage;
public Q_SLOTS:
    void SetListMode(const bool& is_list);
    void SetSearchText(const QString& text);
    void SetShowCompatibilityInGrid(bool show);
    void FocusAndSelectFirstEntryIfNoneIs();
    void OnCompatUpdatedRequested();
    game_info GetSelectedGameInfo();
    void PrintLog(QString entry, QColor textColor);
    void ShowLog(bool show);
private Q_SLOTS:
    void OnColumnClicked(int col);
    void OnParsingFinished();
    void OnRefreshFinished();
    void ShowContextMenu(const QPoint& pos);
    void DoubleClickedSlot(QTableWidgetItem* item);
    void DoubleClickedSlot(const game_info& game);
    void OnCompatFinished();
Q_SIGNALS:
    void FocusToSearchBar();
    void GameListFrameClosed();
    void RequestIconSizeChange(const int& val);
    void Refreshed();
    void NotifyGameSelection(const game_info& game);
    void RequestBoot(const game_info& game);

protected:
    /** Override inherited method from Qt to allow signalling when close happened.*/
    void closeEvent(QCloseEvent* event) override;

private:
    void PushPath(const std::string& path, std::vector<std::string>& legit_paths);
    void CreateConnections();
    bool SearchMatchesApp(const QString& name, const QString& serial, bool fallback = false) const;
    QStringList scanDirectories(const std::vector<std::filesystem::path>& baseDirs, int maxDepth,
                                int currentDepth = 1);
    std::string CurrentSelectionPath();
    void WaitAndAbortRepaintThreads();
    void WaitAndAbortSizeCalcThreads();
    game_info GetGameInfoByMode(const QTableWidgetItem* item) const;
    static game_info GetGameInfoFromItem(const QTableWidgetItem* item);
    // Settings
    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettings> m_emu_settings;
    std::shared_ptr<PersistentSettings> m_persistent_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
    // Objects
    QMainWindow* m_game_dock = nullptr;
    QStackedWidget* m_central_widget = nullptr;
    GameListGrid* m_game_grid = nullptr;  // Game Grid
    GameListTable* m_game_list = nullptr; // Game List
    GameCompatibility* m_game_compat = nullptr;
    ProgressDialog* m_progress_dialog = nullptr;
    // Data
    struct path_entry {
        std::string path;
        bool is_from_file{};
    };
    std::vector<path_entry> m_path_entries;
    QSet<QString> m_hidden_list;
    bool m_show_hidden{false};
    std::vector<game_info> m_game_data;
    QFutureWatcher<void> m_parsing_watcher;
    QFutureWatcher<void> m_refresh_watcher;
    std::shared_mutex m_path_mutex;
    std::set<std::string> m_path_list;
    QSet<QString> m_serials;
    QMutex m_games_mutex;
    lf_queue<game_info> m_games;
    const std::array<int, 1> m_parsing_threads{0};
    // List Mode
    bool m_is_list_layout = true;
    bool m_old_layout_is_list = true;
    // Game List
    QList<QAction*> m_columnActs;
    Qt::SortOrder m_col_sort_order{};
    int m_sort_column{};
    std::map<QString, QString> m_titles;
    std::map<QString, QString> m_notes;
    bool m_initial_refresh_done = false;
    // Search
    QString m_search_text;
    // Icon Size
    int m_icon_size_index = 0;
    // Icons
    QColor m_icon_color;
    QSize m_icon_size;
    qreal m_margin_factor;
    qreal m_text_factor;
    // Logger
    QSplitter* splitter;
    QTextEdit* logDisplay;
    //
    bool m_draw_compat_status_to_grid = false;
    enum class DeleteType { Game, Update, SaveData, DLC, Trophy, ShaderCache };
};
