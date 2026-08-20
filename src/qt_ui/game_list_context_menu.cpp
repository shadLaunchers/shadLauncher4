// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <regex>
#include <set>
#include <system_error>
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
#include <QTimer>
#include <QtConcurrent>
#include <core/user_settings.h>
#include <fmt/core.h>
#include "background_music_player.h"
#include "change_log_dialog.h"
#include "common/key_manager.h"
#include "common/singleton.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/file_format/npbind.h"
#include "core/file_format/psf.h"
#include "core/file_sys/game_backend.h"
#include "core/file_sys/zar_packer.h"
#include "core/ipc/ipc_client.h"
#include "game_categories.h"
#include "game_list_context_menu.h"
#include "game_list_frame.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "localized.h"
#include "npbind_dialog.h"
#include "progress_dialog.h"
#include "qt_utils.h"

#include "cheats_patches_dialog.h"
#include "common/log_analyzer.h"
#include "common/path_util.h"
#include "core/ipc/ipc_client.h"
#include "dlc_viewer_dialog.h"
#include "settings_dialog.h"
#include "sfo_viewer_dialog.h"
#include "trophy_viewer.h"
#include "zarchive_viewer_dialog.h"

bool IsPathWithinAnyDir(const std::filesystem::path& path,
                        const std::vector<std::filesystem::path>& dirs) {
    std::error_code ec;
    const auto canonical_path = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }

    for (const auto& dir : dirs) {
        const auto canonical_dir = std::filesystem::weakly_canonical(dir, ec);
        if (ec) {
            continue;
        }

        std::error_code rel_ec;
        const auto rel = std::filesystem::relative(canonical_path, canonical_dir, rel_ec);
        if (rel_ec || rel.empty()) {
            continue;
        }

        const auto rel_str = rel.generic_string();
        if (rel_str != ".." && rel_str.rfind("../", 0) != 0) {
            return true;
        }
    }
    return false;
}

struct DeleteProgress {
    std::size_t items_done = 0;
    std::size_t items_total = 0;
    std::string current_item;
    bool scanning = false;
};

static bool RemovePathsWithProgress(const std::vector<std::filesystem::path>& targets,
                                    const std::function<bool(const DeleteProgress&)>& progress_cb,
                                    std::string* error_message) {
    namespace fs = std::filesystem;

    const auto canceled = [&] {
        if (error_message) {
            *error_message = "Canceled";
        }
        return false;
    };
    DeleteProgress progress;
    progress.scanning = true;

    std::vector<fs::path> queue;
    for (const auto& target : targets) {
        std::error_code exists_ec;
        if (target.empty() || !fs::exists(target, exists_ec) || exists_ec) {
            continue;
        }

        std::error_code type_ec;
        if (!fs::is_directory(target, type_ec) || type_ec) {
            queue.push_back(target);
            progress.items_done = queue.size();
            progress.current_item = target.filename().string();
            if (progress_cb && !progress_cb(progress)) {
                return canceled();
            }
            continue;
        }

        std::vector<fs::path> children;
        std::error_code walk_ec;
        for (const auto& entry : fs::recursive_directory_iterator(
                 target, fs::directory_options::skip_permission_denied, walk_ec)) {
            if (walk_ec) {
                break;
            }
            children.push_back(entry.path());

            progress.items_done = queue.size() + children.size();
            progress.current_item = entry.path().filename().string();
            if (progress_cb && !progress_cb(progress)) {
                return canceled();
            }
        }
        queue.insert(queue.end(), children.rbegin(), children.rend());
        queue.push_back(target);
    }

    // Phase 2: the actual removal.
    progress.scanning = false;
    progress.items_done = 0;
    progress.items_total = queue.size();
    bool had_error = false;

    for (const auto& item : queue) {
        if (progress_cb) {
            progress.current_item = item.filename().string();
            if (!progress_cb(progress)) {
                return canceled();
            }
        }

        std::error_code remove_ec;
        fs::remove(item, remove_ec);
        progress.items_done++;
        if (remove_ec) {
            had_error = true;
            if (error_message && error_message->empty()) {
                *error_message = remove_ec.message();
            }
        }
    }

    // Final tick so the bar lands on 100% instead of stopping one item short.
    if (progress_cb) {
        progress.current_item.clear();
        progress_cb(progress);
    }

    return !had_error;
}

static void RunDeleteWithProgress(GameListFrame* frame, const QString& title,
                                  std::vector<std::filesystem::path> targets,
                                  const std::function<void()>& on_success) {
    auto* progress = new ProgressDialog(title, QObject::tr("Scanning..."), QObject::tr("Cancel"), 0,
                                        1000, /*delete_on_close=*/true, frame);
    progress->setAutoReset(false);
    progress->setAutoClose(false);
    progress->setMinimumDuration(0);
    progress->SetRange(0, 0); // indeterminate until the scan finishes
    progress->show();

    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(progress, &QProgressDialog::canceled, frame,
                     [cancel_flag] { *cancel_flag = true; });

    struct DeleteResult {
        bool success = false;
        std::string error_message;
    };

    QPointer<ProgressDialog> progress_guard(progress);
    auto* watcher = new QFutureWatcher<DeleteResult>(frame);
    constexpr qint64 kMinVisibleMs = 700;
    auto started =
        std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());

    QObject::connect(watcher, &QFutureWatcher<DeleteResult>::finished, frame,
                     [frame, watcher, progress_guard, title, on_success, started]() {
                         const DeleteResult result = watcher->result();
                         watcher->deleteLater();

                         auto finish = [frame, progress_guard, title, on_success, result] {
                             if (progress_guard) {
                                 progress_guard->close();
                             }

                             if (!result.success) {
                                 if (result.error_message != "Canceled") {
                                     QMessageBox::critical(
                                         frame, title,
                                         QObject::tr("Some files could not be deleted:\n%1")
                                             .arg(QString::fromStdString(result.error_message)));
                                 }
                                 return;
                             }

                             if (on_success) {
                                 on_success();
                             }
                         };

                         const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now() - *started)
                                                  .count();
                         if (result.success && elapsed < kMinVisibleMs) {
                             if (progress_guard) {
                                 progress_guard->SetRange(0, 1000);
                                 progress_guard->SetValue(1000);
                                 progress_guard->setLabelText(QObject::tr("Done."));
                             }
                             QTimer::singleShot(static_cast<int>(kMinVisibleMs - elapsed), frame,
                                                finish);
                             return;
                         }

                         finish();
                     });
    auto last_post = std::make_shared<std::chrono::steady_clock::time_point>();
    auto posted_once = std::make_shared<bool>(false);
    auto showing_scan_range = std::make_shared<bool>(true);

    auto future = QtConcurrent::run([targets, cancel_flag, progress_guard, last_post, posted_once,
                                     showing_scan_range]() -> DeleteResult {
        std::string error_message;
        const bool ok = RemovePathsWithProgress(
            targets,
            [cancel_flag, progress_guard, last_post, posted_once,
             showing_scan_range](const DeleteProgress& p) {
                if (cancel_flag->load()) {
                    return false;
                }
                if (!progress_guard) {
                    return true;
                }
                const auto now = std::chrono::steady_clock::now();
                const bool is_final =
                    !p.scanning && p.items_total > 0 && p.items_done == p.items_total;
                if (*posted_once && !is_final &&
                    now - *last_post < std::chrono::milliseconds(100)) {
                    return true;
                }
                *posted_once = true;
                *last_post = now;

                const int percent = (!p.scanning && p.items_total > 0)
                                        ? static_cast<int>((p.items_done * 1000) / p.items_total)
                                        : 0;

                QMetaObject::invokeMethod(
                    progress_guard.data(),
                    [progress_guard, showing_scan_range, scanning = p.scanning, percent,
                     found = p.items_done, item = p.current_item] {
                        if (!progress_guard) {
                            return;
                        }

                        if (scanning) {
                            progress_guard->setLabelText(
                                QObject::tr("Scanning: %1 items...").arg(found));
                            return;
                        }

                        if (*showing_scan_range) {
                            *showing_scan_range = false;
                            progress_guard->SetRange(0, 1000);
                        }
                        progress_guard->SetValue(percent);
                        progress_guard->setLabelText(
                            item.empty()
                                ? QObject::tr("Deleting...")
                                : QObject::tr("Deleting: %1").arg(QString::fromStdString(item)));
                    },
                    Qt::QueuedConnection);

                return true;
            },
            &error_message);

        return DeleteResult{ok, ok ? std::string() : error_message};
    });

    watcher->setFuture(future);
}

struct DeletePaths {
    QString game;
    QString update;
    QString dlc;
    QString save_data;
    QString shader_cache_dir;
    QString shader_cache_zip;
    QStringList trophy_dirs;
    QStringList trophy_user_files;
};

static DeletePaths ResolveDeletePaths(const std::filesystem::path& addon_install_dir,
                                      const game_info& gameinfo) {
    DeletePaths paths;

    Common::FS::PathToQString(paths.game, gameinfo->info.path);
    Common::FS::PathToQString(paths.update, gameinfo->info.update_path);

    if (!addon_install_dir.empty()) {
        Common::FS::PathToQString(
            paths.dlc,
            addon_install_dir / Common::FS::PathFromQString(paths.game).parent_path().filename());
    }

    const std::string default_user_id =
        std::to_string(UserSettings.GetUserManager().GetDefaultUser().user_id);
    Common::FS::PathToQString(paths.save_data, EmulatorSettings.GetHomeDir() / default_user_id /
                                                   "savedata" / gameinfo->info.save_dir);

    Common::FS::PathToQString(paths.shader_cache_dir,
                              Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
                                  gameinfo->info.serial);
    Common::FS::PathToQString(paths.shader_cache_zip,
                              Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
                                  (gameinfo->info.serial + ".zip"));

    std::filesystem::path npbind_path =
        std::filesystem::path(gameinfo->info.path) / "sce_sys" / "npbind.dat";
    if (const auto resolved =
            Core::FileSys::ResolveGameFilePath(gameinfo->info.path, "sce_sys/npbind.dat")) {
        npbind_path = *resolved;
    }

    NPBindFile npbind;
    if (npbind.Load(npbind_path.string())) {
        const auto& users = UserSettings.GetUserManager().GetAllUsers();

        for (const std::string& np_comm_id : npbind.GetNpCommIds()) {
            if (np_comm_id.empty()) {
                continue;
            }

            QString trophy_dir;
            Common::FS::PathToQString(trophy_dir,
                                      Common::FS::GetUserPath(Common::FS::PathType::UserDir) /
                                          "trophy" / np_comm_id);
            paths.trophy_dirs.append(trophy_dir);

            for (const auto& user : users) {
                QString user_file;
                Common::FS::PathToQString(user_file, EmulatorSettings.GetHomeDir() /
                                                         std::to_string(user.user_id) / "trophy" /
                                                         (np_comm_id + ".xml"));
                paths.trophy_user_files.append(user_file);
            }
        }
    }

    return paths;
}

static bool DeleteTargetExists(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(Common::FS::PathFromQString(path), ec) && !ec;
}

static void RetargetCategories(GameListFrame* frame, const std::string& old_path,
                               const std::string& new_path) {
    if (GameCategories* categories = frame ? frame->GetCategories() : nullptr) {
        categories->Relocate(QString::fromStdString(old_path), QString::fromStdString(new_path));
    }
}

GameListContextMenu::GameListContextMenu(GameListFrame* frame) : QMenu(frame), m_frame(frame) {}

void GameListContextMenu::Show(const game_info& gameinfo, const QPoint& global_pos) {
    GameListFrame* frame = m_frame;
    const DeletePaths delete_paths =
        ResolveDeletePaths(frame->m_emu_settings ? frame->m_emu_settings->GetAddonInstallDir()
                                                 : std::filesystem::path{},
                           gameinfo);

    auto deleteHandler = [frame, gameinfo, delete_paths](GameListFrame::DeleteType type) {
        bool error = false;
        QString folder_path;
        QString message_type;

        const QString& game_path = delete_paths.game;
        const QString& update_path = delete_paths.update;
        const QString& dlc_path = delete_paths.dlc;
        const QString& save_data_path = delete_paths.save_data;

        // Wipes the game together with its update. Shared by "Delete Game" and
        // "Delete Game + Update" so the two can never drift apart. Runs behind
        // a progress dialog since a game install can be tens of gigabytes.
        auto remove_game_and_update = [&] {
            BackgroundMusicPlayer::getInstance().StopMusic();

            std::vector<std::filesystem::path> targets;
            if (!update_path.isEmpty()) {
                targets.push_back(Common::FS::PathFromQString(update_path));
            }
            targets.push_back(Common::FS::PathFromQString(game_path));

            RunDeleteWithProgress(frame, tr("Delete Game"), targets, [frame, gameinfo] {
                auto& game_data = frame->m_game_data;
                game_data.erase(std::remove(game_data.begin(), game_data.end(), gameinfo),
                                game_data.end());
                frame->Refresh(false);
            });
        };

        const bool has_update = !update_path.isEmpty() &&
                                std::filesystem::exists(Common::FS::PathFromQString(update_path));

        switch (type) {
        case GameListFrame::DeleteType::Game:
            if (has_update) {
                const QMessageBox::StandardButton reply = QMessageBox::question(
                    frame, tr("Delete Game"),
                    tr("Are you sure you want to delete %1's game directory?\n\n"
                       "Its update/patch directory will be deleted too.")
                        .arg(QString::fromStdString(gameinfo->info.name)),
                    QMessageBox::Yes | QMessageBox::No);
                if (reply != QMessageBox::Yes) {
                    return;
                }

                remove_game_and_update();
                return;
            }

            BackgroundMusicPlayer::getInstance().StopMusic();
            folder_path = game_path;
            message_type = tr("Game");
            break;

        case GameListFrame::DeleteType::Update:
            if (!has_update) {
                QMessageBox::critical(frame, tr("Error"), tr("This game has no update to delete!"));
                return;
            }
            folder_path = update_path;
            message_type = tr("Update");
            break;

        case GameListFrame::DeleteType::GameAndUpdate: {
            if (!has_update) {
                QMessageBox::critical(frame, tr("Error"), tr("This game has no update to delete!"));
                return;
            }

            const QMessageBox::StandardButton reply = QMessageBox::question(
                frame, tr("Delete Game + Update"),
                tr("Are you sure you want to delete %1's game and update/patch "
                   "directories? This cannot be undone.")
                    .arg(QString::fromStdString(gameinfo->info.name)),
                QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }

            remove_game_and_update();
            return;
        }

        case GameListFrame::DeleteType::DLC:
            if (!std::filesystem::exists(Common::FS::PathFromQString(dlc_path))) {
                QMessageBox::critical(frame, tr("Error"), tr("This game has no DLC to delete!"));
                return;
            }
            folder_path = dlc_path;
            message_type = tr("DLC");
            break;

        case GameListFrame::DeleteType::SaveData:
            if (!std::filesystem::exists(Common::FS::PathFromQString(save_data_path))) {
                QMessageBox::critical(frame, tr("Error"),
                                      tr("This game has no save data to delete!"));
                return;
            }
            folder_path = save_data_path;
            message_type = tr("Save Data");
            break;

        case GameListFrame::DeleteType::Trophy: {
            QStringList existing_dirs;
            for (const QString& dir : delete_paths.trophy_dirs) {
                if (DeleteTargetExists(dir)) {
                    existing_dirs.append(dir);
                }
            }

            QStringList existing_files;
            for (const QString& file : delete_paths.trophy_user_files) {
                if (DeleteTargetExists(file)) {
                    existing_files.append(file);
                }
            }

            if (existing_dirs.isEmpty() && existing_files.isEmpty()) {
                QMessageBox::critical(frame, tr("Error"),
                                      tr("This game has no saved trophies to delete!"));
                return;
            }

            const QMessageBox::StandardButton reply = QMessageBox::question(
                frame, tr("Delete Trophy"),
                tr("Are you sure you want to delete %1's trophy data?\n\n"
                   "This removes the unpacked trophy files and the unlocked trophies of "
                   "every user. Any other copy of this game shares the same trophy data "
                   "and will lose it too. This cannot be undone.")
                    .arg(QString::fromStdString(gameinfo->info.name)),
                QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }

            for (const QString& dir : existing_dirs) {
                std::error_code trophy_ec;
                std::filesystem::remove_all(Common::FS::PathFromQString(dir), trophy_ec);
                if (trophy_ec) {
                    error = true;
                }
            }

            for (const QString& file : existing_files) {
                std::error_code trophy_ec;
                std::filesystem::remove(Common::FS::PathFromQString(file), trophy_ec);
                if (trophy_ec) {
                    error = true;
                }
            }

            if (error) {
                QMessageBox::critical(frame, tr("Error"),
                                      tr("Some trophy data could not be deleted."));
            } else {
                QMessageBox::information(frame, tr("Trophy"),
                                         tr("Trophy data deleted successfully."));
            }
            return;
        }

        case GameListFrame::DeleteType::ShaderCache: {
            const auto dir_path = Common::FS::PathFromQString(delete_paths.shader_cache_dir);
            const auto zip_path = Common::FS::PathFromQString(delete_paths.shader_cache_zip);

            bool has_dir = std::filesystem::exists(dir_path);
            bool has_zip = std::filesystem::exists(zip_path);

            if (!has_dir && !has_zip) {
                QMessageBox::critical(frame, tr("Error"),
                                      tr("This game has no Shader Cache to delete!"));
                return;
            }

            if (has_dir)
                std::filesystem::remove_all(dir_path);
            if (has_zip)
                std::filesystem::remove(zip_path);

            QMessageBox::information(frame, tr("Shader Cache"),
                                     tr("Shader cache deleted successfully."));
            return;
        }

        case GameListFrame::DeleteType::MetadataCache: {
            QMessageBox::StandardButton reply =
                QMessageBox::question(frame, tr("Clear Metadata Cache"),
                                      tr("Clear the cached name/serial/icon/size info for %1?\n\n"
                                         "It will be re-read from disk on the next refresh.")
                                          .arg(QString::fromStdString(gameinfo->info.name)),
                                      QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }
            if (frame->m_info_cache) {
                frame->m_info_cache->ClearGame(gameinfo->info.path);
            }
            frame->Refresh(true);
            return;
        }
        }

        if (folder_path.isEmpty()) {
            return;
        }

        QMessageBox::StandardButton reply = QMessageBox::question(
            frame, tr("Delete %1").arg(message_type),
            tr("Are you sure you want to delete %1's %2 directory?")
                .arg(QString::fromStdString(gameinfo->info.name), message_type),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            const std::filesystem::path path_to_delete = Common::FS::PathFromQString(folder_path);
            const GameListFrame::DeleteType delete_type = type;

            RunDeleteWithProgress(
                frame, tr("Delete %1").arg(message_type), {path_to_delete},
                [frame, gameinfo, delete_type] {
                    if (delete_type == GameListFrame::DeleteType::Game) {
                        auto& game_data = frame->m_game_data;
                        game_data.erase(std::remove(game_data.begin(), game_data.end(), gameinfo),
                                        game_data.end());
                        frame->Refresh(false);
                    } else if (delete_type == GameListFrame::DeleteType::Update) {
                        gameinfo->info.update_path.clear();
                        gameinfo->info.size_on_disk = UINT64_MAX;
                        frame->Refresh(false);
                    }
                });
        }
    };

    auto convertPathToZArchiveHandler =
        [frame](const std::filesystem::path& source_path, const QString& display_name,
                const QString& dialog_title, const QString& extra_note,
                const std::function<void(const std::filesystem::path&)>& on_success) {
            if (Core::FileSys::IsZArchiveFile(source_path)) {
                QMessageBox::information(frame, dialog_title,
                                         tr("This is already packed as a ZArchive."));
                return;
            }

            std::error_code dir_ec;
            if (!std::filesystem::is_directory(source_path, dir_ec) || dir_ec) {
                QMessageBox::critical(frame, dialog_title,
                                      tr("This folder could not be found on disk."));
                return;
            }

            QString default_output;
            Common::FS::PathToQString(default_output,
                                      source_path.parent_path() /
                                          (source_path.filename().string() + ".zar"));

            const QString output_path_str =
                QFileDialog::getSaveFileName(frame, tr("Convert %1 to ZArchive").arg(display_name),
                                             default_output, tr("ZArchive Files (*.zar)"));
            if (output_path_str.isEmpty()) {
                return;
            }

            std::filesystem::path output_path = Common::FS::PathFromQString(output_path_str);
            if (output_path.extension() != ".zar") {
                output_path += ".zar";
            }

            std::error_code exists_ec;
            if (std::filesystem::exists(output_path, exists_ec) && !exists_ec) {
                const auto overwrite_reply = QMessageBox::question(
                    frame, dialog_title,
                    tr("%1 already exists. Overwrite it?")
                        .arg(QString::fromStdString(output_path.filename().string())),
                    QMessageBox::Yes | QMessageBox::No);
                if (overwrite_reply != QMessageBox::Yes) {
                    return;
                }
            }

            // clang-format off
        QString confirm_message =
            tr("This will pack \"%1\" into a single read-only .zar archive. Depending on the "
               "size this can take a while, and the archive will temporarily need as "
               "much free disk space as the original.\n\n"
               "The original folder is left untouched until conversion succeeds, you'll be "
               "asked afterward whether to delete it.").arg(display_name);
        if (!extra_note.isEmpty()) {
            confirm_message += extra_note;
        }
        confirm_message += tr("\n\nContinue?");
        const auto confirm_reply = QMessageBox::question(frame, dialog_title, confirm_message,
                                                          QMessageBox::Yes | QMessageBox::No);
            // clang-format on
            if (confirm_reply != QMessageBox::Yes) {
                return;
            }

            auto* progress =
                new ProgressDialog(dialog_title, tr("Packing %1...").arg(display_name),
                                   tr("Cancel"), 0, 1000, /*delete_on_close=*/true, frame);
            progress->SetValue(0);
            progress->show();

            auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
            connect(progress, &QProgressDialog::canceled, frame,
                    [cancel_flag] { *cancel_flag = true; });

            struct ConvertZarResult {
                bool success = false;
                std::string error_message;
            };

            QPointer<ProgressDialog> progress_guard(progress);
            auto* watcher = new QFutureWatcher<ConvertZarResult>(frame);

            connect(watcher, &QFutureWatcher<ConvertZarResult>::finished, frame,
                    [frame, watcher, progress_guard, source_path, output_path, dialog_title,
                     on_success]() {
                        const ConvertZarResult result = watcher->result();
                        watcher->deleteLater();

                        if (progress_guard) {
                            progress_guard->close();
                        }

                        if (!result.success) {
                            if (result.error_message != "Canceled") {
                                QMessageBox::critical(
                                    frame, dialog_title,
                                    tr("Failed to convert to ZArchive:\n%1")
                                        .arg(QString::fromStdString(result.error_message)));
                            }
                            return;
                        }

                        QString source_qpath;
                        Common::FS::PathToQString(source_qpath, source_path);
                        const auto delete_reply = QMessageBox::question(
                            frame, dialog_title,
                            tr("Conversion finished. Delete the original folder now to free "
                               "up disk space?\n\n%1")
                                .arg(source_qpath),
                            QMessageBox::Yes | QMessageBox::No);
                        if (delete_reply == QMessageBox::Yes) {
                            BackgroundMusicPlayer::getInstance().StopMusic();

                            std::error_code remove_ec;
                            std::filesystem::remove_all(source_path, remove_ec);
                            if (remove_ec) {
                                QMessageBox::warning(
                                    frame, dialog_title,
                                    tr("The archive was created, but the original folder could "
                                       "not be fully deleted. You can remove it manually."));
                            }
                        }

                        if (on_success) {
                            on_success(output_path);
                        }
                    });

            auto future = QtConcurrent::run(
                [source_path, output_path, cancel_flag, progress_guard]() -> ConvertZarResult {
                    std::string error_message;
                    const bool ok = Core::FileSys::PackDirectoryToZArchive(
                        source_path, output_path,
                        [cancel_flag, progress_guard](const Core::FileSys::PackProgress& p) {
                            if (progress_guard) {
                                const int percent =
                                    p.bytes_total > 0
                                        ? static_cast<int>((p.bytes_done * 1000) / p.bytes_total)
                                        : 0;
                                QMetaObject::invokeMethod(
                                    progress_guard.data(),
                                    [progress_guard, percent, file = p.current_file]() {
                                        if (!progress_guard) {
                                            return;
                                        }
                                        progress_guard->SetValue(percent);
                                        progress_guard->setLabelText(
                                            tr("Packing: %1").arg(QString::fromStdString(file)));
                                    },
                                    Qt::QueuedConnection);
                            }
                            return !cancel_flag->load();
                        },
                        &error_message);

                    return ConvertZarResult{ok, ok ? std::string() : error_message};
                });

            watcher->setFuture(future);
        };

    auto refreshOneGameLight = [frame](const game_info& game) {
        if (game) {
            game->info.size_on_disk = UINT64_MAX;
        }
        frame->Refresh(false);
    };

    auto applyNewPathIfWatched = [frame](const std::filesystem::path& new_path,
                                         const std::function<void()>& apply_and_refresh,
                                         const QString& dialog_title) {
        const auto install_dirs = frame->m_emu_settings
                                      ? frame->m_emu_settings->GetGameInstallDirs()
                                      : std::vector<std::filesystem::path>{};
        if (IsPathWithinAnyDir(new_path.parent_path(), install_dirs)) {
            apply_and_refresh();
            return;
        }

        QMessageBox::information(
            frame, dialog_title,
            tr("The archive was saved to a folder that isn't one of your configured "
               "game folders, so it won't show up in the game list. Move it into a "
               "configured folder, or add this folder under Settings, if you'd like "
               "it to appear."));
        frame->Refresh(true);
    };

    auto convertToZArchiveHandler = [frame, convertPathToZArchiveHandler, refreshOneGameLight,
                                     applyNewPathIfWatched](const game_info& game) {
        const std::filesystem::path source_path(game->info.path);
        const QString game_name = QString::fromStdString(game->info.name);

        QString extra_note;
        if (!game->info.update_path.empty() && !Core::FileSys::IsZArchiveFile(source_path)) {
            extra_note = tr("\n\nThis game has a separate update/patch folder. Only the base game "
                            "will be archived; the update/patch folder will not be included and "
                            "will be left as-is. Use \"Convert Update to ZArchive\" separately if "
                            "you'd like to archive it too.");
        }

        convertPathToZArchiveHandler(
            source_path, game_name, tr("Convert to ZArchive"), extra_note,
            [frame, game, refreshOneGameLight,
             applyNewPathIfWatched](const std::filesystem::path& new_path) {
                applyNewPathIfWatched(
                    new_path,
                    [frame, game, new_path, refreshOneGameLight] {
                        const std::string moved_path = GUI::Utils::NormalizePath(new_path);
                        RetargetCategories(frame, game->info.path, moved_path);
                        game->info.path = moved_path;
                        refreshOneGameLight(game);
                    },
                    tr("Convert to ZArchive"));
            });
    };

    auto convertUpdateToZArchiveHandler = [frame, convertPathToZArchiveHandler, refreshOneGameLight,
                                           applyNewPathIfWatched](const game_info& game) {
        if (game->info.update_path.empty()) {
            QMessageBox::information(
                frame, tr("Convert Update to ZArchive"),
                tr("This game has no separate update/patch folder to archive."));
            return;
        }

        const std::filesystem::path source_path(game->info.update_path);
        const QString display_name = tr("%1 Update").arg(QString::fromStdString(game->info.name));

        convertPathToZArchiveHandler(source_path, display_name, tr("Convert Update to ZArchive"),
                                     QString(),
                                     [game, refreshOneGameLight, applyNewPathIfWatched](
                                         const std::filesystem::path& new_path) {
                                         applyNewPathIfWatched(
                                             new_path,
                                             [game, new_path, refreshOneGameLight] {
                                                 game->info.update_path = new_path.string();
                                                 refreshOneGameLight(game);
                                             },
                                             tr("Convert Update to ZArchive"));
                                     });
    };

    auto convertFromZArchiveHandler = [frame, applyNewPathIfWatched](const game_info& game) {
        const std::filesystem::path source_path(game->info.path);

        if (!Core::FileSys::IsZArchiveFile(source_path)) {
            QMessageBox::information(frame, tr("Convert from ZArchive"),
                                     tr("This game is not packed as a ZArchive."));
            return;
        }

        const QString game_name = QString::fromStdString(game->info.name);

        QString default_dir;
        Common::FS::PathToQString(default_dir, source_path.parent_path());

        const QString output_path_str =
            QFileDialog::getExistingDirectory(frame, tr("Extract %1 to Folder").arg(game_name),
                                              default_dir, QFileDialog::ShowDirsOnly);
        if (output_path_str.isEmpty()) {
            return;
        }

        std::filesystem::path output_path = Common::FS::PathFromQString(output_path_str);
        std::error_code exists_ec;
        if (std::filesystem::exists(output_path, exists_ec) && !exists_ec) {
            bool has_entries = false;
            std::error_code it_ec;
            for (const auto& entry : std::filesystem::directory_iterator(output_path, it_ec)) {
                (void)entry;
                has_entries = true;
                break;
            }
            if (has_entries) {
                output_path /= source_path.stem();
            }
        }

        std::error_code target_exists_ec;
        if (std::filesystem::exists(output_path, target_exists_ec) && !target_exists_ec) {
            bool target_has_entries = false;
            std::error_code it_ec;
            for (const auto& entry : std::filesystem::directory_iterator(output_path, it_ec)) {
                (void)entry;
                target_has_entries = true;
                break;
            }
            if (target_has_entries) {
                QMessageBox::critical(
                    frame, tr("Convert from ZArchive"),
                    tr("The destination folder \"%1\" already exists and is not empty.")
                        .arg(QString::fromStdString(output_path.filename().string())));
                return;
            }
        }

        // clang-format off
        const auto confirm_reply = QMessageBox::question(
            frame, tr("Convert from ZArchive"),
            tr("This will extract \"%1\" into a regular folder. Depending on the "
               "game's size this can take a while, and the folder will need as much free "
               "disk space as the game itself.\n\n"
               "The original .zar archive is left untouched until extraction succeeds, "
               "you'll be asked afterward whether to delete it.\n\nContinue?").arg(game_name),
            QMessageBox::Yes | QMessageBox::No);
        // clang-format on
        if (confirm_reply != QMessageBox::Yes) {
            return;
        }

        auto* progress =
            new ProgressDialog(tr("Convert from ZArchive"), tr("Extracting %1...").arg(game_name),
                               tr("Cancel"), 0, 1000, /*delete_on_close=*/true, frame);
        progress->SetValue(0);
        progress->show();

        auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
        connect(progress, &QProgressDialog::canceled, frame,
                [cancel_flag] { *cancel_flag = true; });

        struct UnpackZarResult {
            bool success = false;
            std::string error_message;
        };

        QPointer<ProgressDialog> progress_guard(progress);
        auto* watcher = new QFutureWatcher<UnpackZarResult>(frame);

        connect(watcher, &QFutureWatcher<UnpackZarResult>::finished, frame,
                [frame, watcher, progress_guard, source_path, output_path, game,
                 applyNewPathIfWatched]() {
                    const UnpackZarResult result = watcher->result();
                    watcher->deleteLater();

                    if (progress_guard) {
                        progress_guard->close();
                    }

                    if (!result.success) {
                        if (result.error_message != "Canceled") {
                            QMessageBox::critical(
                                frame, tr("Convert from ZArchive"),
                                tr("Failed to extract game from ZArchive:\n%1")
                                    .arg(QString::fromStdString(result.error_message)));
                        }
                        return;
                    }

                    QString source_qpath;
                    Common::FS::PathToQString(source_qpath, source_path);
                    const auto delete_reply = QMessageBox::question(
                        frame, tr("Convert from ZArchive"),
                        tr("Extraction finished. Delete the original .zar archive now to free "
                           "up disk space?\n\n%1")
                            .arg(source_qpath),
                        QMessageBox::Yes | QMessageBox::No);
                    if (delete_reply == QMessageBox::Yes) {
                        BackgroundMusicPlayer::getInstance().StopMusic();

                        std::error_code remove_ec;
                        std::filesystem::remove(source_path, remove_ec);
                        if (remove_ec) {
                            QMessageBox::warning(
                                frame, tr("Convert from ZArchive"),
                                tr("The game was extracted, but the original .zar archive "
                                   "could not be deleted. You can remove it manually."));
                        }
                    }

                    applyNewPathIfWatched(
                        output_path,
                        [frame, game, output_path] {
                            const std::string moved_path = GUI::Utils::NormalizePath(output_path);
                            RetargetCategories(frame, game->info.path, moved_path);
                            game->info.path = moved_path;
                            game->info.size_on_disk = UINT64_MAX;
                            frame->Refresh(false);
                        },
                        tr("Convert from ZArchive"));
                });

        auto future = QtConcurrent::run(
            [source_path, output_path, cancel_flag, progress_guard]() -> UnpackZarResult {
                std::string error_message;
                const bool ok = Core::FileSys::UnpackZArchiveToDirectory(
                    source_path, output_path,
                    [cancel_flag, progress_guard](const Core::FileSys::UnpackProgress& p) {
                        if (progress_guard) {
                            const int percent =
                                p.files_total > 0
                                    ? static_cast<int>((p.files_done * 1000) / p.files_total)
                                    : 0;
                            QMetaObject::invokeMethod(
                                progress_guard.data(),
                                [progress_guard, percent, file = p.current_file]() {
                                    if (!progress_guard) {
                                        return;
                                    }
                                    progress_guard->SetValue(percent);
                                    progress_guard->setLabelText(
                                        tr("Extracting: %1").arg(QString::fromStdString(file)));
                                },
                                Qt::QueuedConnection);
                        }
                        return !cancel_flag->load();
                    },
                    &error_message);

                return UnpackZarResult{ok, ok ? std::string() : error_message};
            });

        watcher->setFuture(future);
    };

    GameInfo current_game = gameinfo->info;
    const QString serial = QString::fromStdString(current_game.serial);
    const QString name = QString::fromStdString(current_game.name).simplified();
    // Per game data (hidden flag, notes, categories) is keyed on the install
    // path: the same serial can be installed in several folders and each copy
    // is its own row.
    const QString game_key = GUI::Utils::GameKeyOf(current_game);
    const std::string game_path = current_game.path;

    QMenu* launch_menu = addMenu(tr("&Launch game"));
    QAction* launch_default = launch_menu->addAction(tr("&Launch game with current settings"));
    connect(launch_default, &QAction::triggered, frame,
            [frame, gameinfo] { frame->RequestBoot(gameinfo); });

    QAction* launch_clean = launch_menu->addAction(tr("&Launch game with default settings"));
    connect(launch_clean, &QAction::triggered, frame, [frame, gameinfo] {
        QStringList args = {"--config-clean"};
        frame->RequestBoot(gameinfo, args);
    });

    QAction* launch_global = launch_menu->addAction(tr("&Launch game with global settings"));
    connect(launch_global, &QAction::triggered, frame, [frame, gameinfo] {
        QStringList args = {"--config-global"};
        frame->RequestBoot(gameinfo, args);
    });

    QAction* configure = addAction(gameinfo->has_custom_config
                                       ? tr("&Change Custom Configuration")
                                       : tr("&Create Custom Configuration From Global Settings"));

    // this will work only for separate updates install (-UPDATE or -patch folders)
    const std::string update_path = current_game.update_path;
    if (!update_path.empty()) {
        QAction* change_log = addAction(tr("&View Changelog"));
        // changelog
        connect(change_log, &QAction::triggered, frame, [frame, update_path] {
            const s32 language_index = GUIApplication::getLanguageId();
            const std::string localized_changelog =
                fmt::format("changeinfo_%02d.xml", language_index);
            auto changelog_resolved = Core::FileSys::ResolveGameFilePath(
                update_path, "sce_sys/changeinfo/" + localized_changelog);
            if (!changelog_resolved) {
                changelog_resolved = Core::FileSys::ResolveGameFilePath(
                    update_path, "sce_sys/changeinfo/changeinfo.xml");
            }
            if (changelog_resolved) {
                ChangelogDialog dialog(frame, QString::fromStdString(changelog_resolved->string()));
                dialog.exec();
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
                QAction* change_log = addAction(tr("&View Changelog"));
                connect(change_log, &QAction::triggered, frame, [frame, changelog_path] {
                    ChangelogDialog dialog(frame, QString::fromStdString(changelog_path));
                    dialog.exec();
                });
            }
        }
    }
    // Open Menu
    QMenu* open_menu = addMenu(tr("&Open Folder"));
    auto openFolderForPath = [](const std::string& path) {
        if (path.empty()) {
            return;
        }

        std::error_code ec;
        std::filesystem::path fs_path = std::filesystem::absolute(std::filesystem::path(path), ec);
        if (ec) {
            fs_path = std::filesystem::path(path);
        }

        if (std::filesystem::is_regular_file(fs_path, ec) && !ec) {
            fs_path = fs_path.parent_path();
        }

        if (fs_path.empty() || !std::filesystem::exists(fs_path, ec) || ec) {
            return;
        }

        QString qpath;
        Common::FS::PathToQString(qpath, fs_path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
    };

    QAction* open_game_path = open_menu->addAction(tr("&Open Game Folder"));
    connect(open_game_path, &QAction::triggered, frame,
            [current_game, openFolderForPath] { openFolderForPath(current_game.path); });
    if (!update_path.empty()) {
        QAction* open_update_path = open_menu->addAction(tr("&Open Update Folder"));
        connect(open_update_path, &QAction::triggered, frame,
                [update_path, openFolderForPath] { openFolderForPath(update_path); });
    }
    QAction* open_log_folder = open_menu->addAction(tr("&Open Log Folder"));
    connect(open_log_folder, &QAction::triggered, frame, [frame, serial] {
        // Get log folder path
        QString logPath;
        Common::FS::PathToQString(logPath, Common::FS::GetUserPath(Common::FS::PathType::LogDir));

        if (!frame->m_emu_settings->IsLogSeparate()) {
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
    QAction* sfo_view = addAction(tr("&SFO viewer"));
    connect(sfo_view, &QAction::triggered, frame, [frame, current_game] {
        const std::string base_path =
            !current_game.update_path.empty() ? current_game.update_path : current_game.path;
        QString sfo_path;
        if (const auto resolved =
                Core::FileSys::ResolveGameFilePath(base_path, "sce_sys/param.sfo")) {
            Common::FS::PathToQString(sfo_path, *resolved);
        } else {
            sfo_path = QString::fromStdString(base_path) + "/sce_sys/param.sfo";
        }
        SFOViewerDialog dialog(frame, sfo_path);
        dialog.exec();
    });

    // Desktop shortcut
    QAction* create_shortcut = addAction(tr("&Create Desktop Shortcut"));
    connect(create_shortcut, &QAction::triggered, frame, [frame, current_game] {
        if (frame->m_gui_settings->GetValue(GUI::version_manager_versionSelected)
                .toString()
                .isEmpty()) {
            QMessageBox::information(frame, tr("No Version Selected"),
                                     tr("Select a version first"));
            return;
        }

        QString version =
            frame->m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
        QString path = frame->m_gui_settings->GetVersionExecutablePath(version);
        frame->requestShortcut(current_game);
    });

    QAction* npbind_view = addAction(tr("&npbind.dat viewer"));
    connect(npbind_view, &QAction::triggered, frame, [frame, current_game] {
        const std::string base_path =
            !current_game.update_path.empty() ? current_game.update_path : current_game.path;
        QString npbind_path;
        if (const auto resolved =
                Core::FileSys::ResolveGameFilePath(base_path, "sce_sys/npbind.dat")) {
            Common::FS::PathToQString(npbind_path, *resolved);
        } else {
            npbind_path = QString::fromStdString(base_path) + "/sce_sys/npbind.dat";
        }
        NpBindDialog dialog(frame, npbind_path);
        dialog.exec();
    });

    QAction* cheats_view = addAction(tr("&Cheats & Patches"));
    connect(cheats_view, &QAction::triggered, frame, [frame, current_game] {
        QString gameName = QString::fromStdString(current_game.name);
        QString gameSerial = QString::fromStdString(current_game.serial);
        QString gameVersion = QString::fromStdString(current_game.app_ver);
        QString gameSize = GUI::Utils::FormatByteSize(current_game.size_on_disk);
        QString iconPath;
        Common::FS::PathToQString(iconPath, current_game.icon_path);
        QPixmap gameImage(iconPath);
        CheatsPatches* cheatsPatches =
            new CheatsPatches(frame->m_gui_settings, frame->m_ipc_client, gameName, gameSerial,
                              gameVersion, gameSize, gameImage);
        cheatsPatches->show();
        connect(frame, &QWidget::destroyed, cheatsPatches,
                [cheatsPatches]() { cheatsPatches->deleteLater(); });
    });

    QAction* trophy_viewer = addAction(tr("&Trophy Viewer"));
    connect(trophy_viewer, &QAction::triggered, frame, [frame, current_game] {
        const auto& user_key_vec =
            KeyManager::GetInstance()->GetAllKeys().TrophyKeySet.ReleaseTrophyKey;

        if (user_key_vec.size() != 16) {
            // turn clang format off to maintain one string line for easy translations
            // clang-format off
            QMessageBox::critical(nullptr, tr("Error"), tr("A trophy key is required to use the Trophy Viewer. This can be inputted by clicking Utilities - Crypto Key Manager"));
            // clang-format on
            return;
        }

        if (frame->m_game_data.empty()) {
            QMessageBox::information(
                frame, tr("Trophy Viewer"),
                tr("No games found. Please add your games to your library first."));
            return;
        }

        QString trophyPath, gameTrpPath;
        Common::FS::PathToQString(trophyPath, current_game.serial);
        Common::FS::PathToQString(gameTrpPath, current_game.path);

        if (!current_game.update_path.empty()) {
            Common::FS::PathToQString(gameTrpPath, current_game.update_path);
        }

        QVector<TrophyGameInfo> allTrophyGames;
        for (const auto& game : frame->m_game_data) {
            TrophyGameInfo gameInfo;
            gameInfo.name = QString::fromStdString(game->info.name);
            Common::FS::PathToQString(gameInfo.trophyPath, game->info.serial);
            Common::FS::PathToQString(gameInfo.gameTrpPath, game->info.path);

            if (!game->info.update_path.empty()) {
                Common::FS::PathToQString(gameInfo.gameTrpPath, game->info.update_path);
            }

            allTrophyGames.append(gameInfo);
        }

        QString gameName = QString::fromStdString(current_game.name);
        TrophyViewer* trophyViewer = new TrophyViewer(frame->m_gui_settings, trophyPath,
                                                      gameTrpPath, gameName, allTrophyGames);
        trophyViewer->show();
    });

    QAction* dlc_viewer = addAction(tr("&DLC Viewer"));
    connect(dlc_viewer, &QAction::triggered, frame, [frame, current_game] {
        const std::filesystem::path addon_install_dir =
            frame->m_emu_settings ? frame->m_emu_settings->GetAddonInstallDir()
                                  : std::filesystem::path{};

        QString gameName = QString::fromStdString(current_game.name);
        QString gameSerial = QString::fromStdString(current_game.serial);
        const std::filesystem::path gameRootPath = current_game.path;
        const std::filesystem::path gameUpdatePath = current_game.update_path;

        DlcViewerDialog* dlcViewer = new DlcViewerDialog(frame, gameName, gameSerial, gameRootPath,
                                                         gameUpdatePath, addon_install_dir);
        dlcViewer->setAttribute(Qt::WA_DeleteOnClose);
        dlcViewer->show();
    });

    /* TODO
    const auto valid_users = UserManagement.GetValidUsers();
    for (const auto& user : valid_users) {
        QString user_label =
            QString("%1 (ID: %2)").arg(QString::fromStdString(user.user_name)).arg(user.user_id);
        QAction* user_action = trophy_viewer->addAction(user_label);
        connect(user_action, &QAction::triggered, frame, [frame, user, current_game] {

        });
    }
    */

    // Manage Game Menu
    QMenu* manage_game_menu = addMenu(tr("&Manage Game"));

    QAction* hide_serial = manage_game_menu->addAction(tr("&Hide From Game List"));
    hide_serial->setCheckable(true);
    hide_serial->setChecked(frame->m_hidden_list.contains(game_key));
    QAction* edit_notes = manage_game_menu->addAction(tr("&Add/Edit Tooltip Notes"));
    QAction* edit_title = manage_game_menu->addAction(tr("&Rename in Game List..."));
    QAction* convert_to_zar = manage_game_menu->addAction(tr("&Convert to ZArchive (.zar)..."));
    convert_to_zar->setVisible(
        !Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.path)));
    QAction* convert_update_to_zar =
        manage_game_menu->addAction(tr("Convert &Update to ZArchive (.zar)..."));
    convert_update_to_zar->setVisible(
        !current_game.update_path.empty() &&
        !Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.update_path)));
    QAction* convert_from_zar = manage_game_menu->addAction(tr("&Extract from ZArchive (.zar)..."));
    convert_from_zar->setVisible(
        Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.path)));
    QAction* browse_zar = manage_game_menu->addAction(tr("&Browse ZArchive Contents..."));
    browse_zar->setVisible(Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.path)));
    QAction* browse_update_zar =
        manage_game_menu->addAction(tr("Browse &Update ZArchive Contents..."));
    browse_update_zar->setVisible(
        !current_game.update_path.empty() &&
        Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.update_path)));

    // Categories menu
    QMenu* category_menu = addMenu(tr("&Categories"));
    if (GameCategories* categories = frame->GetCategories()) {
        const GameKey category_key = GameCategories::KeyFor(current_game);
        const QStringList category_names = categories->Names();
        const QStringList current_categories = categories->CategoriesOf(category_key);

        // When a category tab is open, "moving" means moving out of that tab.
        // Anywhere else it means "put the game in this category and nothing else".
        const QString active_tab = frame->CurrentCategory();
        const bool move_from_tab = !active_tab.isEmpty() && current_categories.contains(active_tab);

        auto escape_mnemonics = [](const QString& text) {
            return QString(text).replace('&', "&&");
        };

        for (const QString& category : category_names) {
            QAction* category_action = category_menu->addAction(escape_mnemonics(category));
            category_action->setCheckable(true);
            category_action->setChecked(current_categories.contains(category));
            connect(category_action, &QAction::toggled, frame,
                    [categories, category_key, category](bool checked) {
                        categories->SetMembership(category_key, category, checked);
                    });
        }

        if (!category_names.isEmpty()) {
            category_menu->addSeparator();

            QMenu* move_menu = category_menu->addMenu(
                move_from_tab ? tr("&Move From \"%1\" To").arg(escape_mnemonics(active_tab))
                              : tr("&Move To"));
            move_menu->setToolTipsVisible(true);

            for (const QString& category : category_names) {
                if (move_from_tab && category == active_tab) {
                    continue;
                }

                QAction* move_action = move_menu->addAction(escape_mnemonics(category));

                if (move_from_tab) {
                    move_action->setToolTip(tr("Take %1 out of \"%2\" and put it in \"%3\".")
                                                .arg(name, active_tab, category));
                    connect(move_action, &QAction::triggered, frame,
                            [categories, category_key, active_tab, category] {
                                categories->MoveBetween(category_key, active_tab, category);
                            });
                } else {
                    // Already the game's one and only category, nothing to do.
                    move_action->setEnabled(current_categories != QStringList{category});
                    move_action->setToolTip(
                        tr("Put %1 in \"%2\" only, removing it from its other categories.")
                            .arg(name, category));
                    connect(move_action, &QAction::triggered, frame,
                            [categories, category_key, category] {
                                categories->MoveTo(category_key, category);
                            });
                }
            }

            move_menu->addSeparator();

            QAction* move_to_new = move_menu->addAction(tr("&New Category..."));
            connect(move_to_new, &QAction::triggered, frame,
                    [frame, categories, category_key, active_tab, move_from_tab] {
                        const QString created = frame->PromptNewCategory();
                        if (created.isEmpty()) {
                            return;
                        }
                        if (move_from_tab) {
                            categories->MoveBetween(category_key, active_tab, created);
                        } else {
                            categories->MoveTo(category_key, created);
                        }
                    });

            QAction* uncategorize = move_menu->addAction(
                move_from_tab ? tr("&Out Of \"%1\"").arg(escape_mnemonics(active_tab))
                              : tr("&No Category"));
            uncategorize->setEnabled(!current_categories.isEmpty());
            connect(uncategorize, &QAction::triggered, frame,
                    [categories, category_key, active_tab, move_from_tab] {
                        if (move_from_tab) {
                            categories->SetMembership(category_key, active_tab, false);
                        } else {
                            categories->MoveTo(category_key, QString());
                        }
                    });

            category_menu->addSeparator();
        }

        QAction* new_category = category_menu->addAction(tr("&New Category..."));
        connect(new_category, &QAction::triggered, frame,
                [frame, category_key] { frame->PromptNewCategory(&category_key); });
    }

    // Copy Info menu
    QMenu* info_menu = addMenu(tr("&Copy Info"));
    QAction* copy_info = info_menu->addAction(tr("&Copy Name + Serial"));
    QAction* copy_name = info_menu->addAction(tr("&Copy Name"));
    QAction* copy_serial = info_menu->addAction(tr("&Copy Serial"));

    // Delete
    QMenu* delete_menu = addMenu(tr("&Delete..."));
    // Only offer what is actually there: an entry whose directory is missing can
    // do nothing but pop an error, so it stays hidden. The game directory always
    // exists (it is what the row was built from) and the metadata cache is ours,
    // so those two are always shown.
    const bool has_update = DeleteTargetExists(delete_paths.update);

    QAction* delete_game = delete_menu->addAction(tr("&Delete Game"));
    QAction* delete_update = delete_menu->addAction(tr("&Delete Update"));
    delete_update->setVisible(has_update);
    QAction* delete_game_and_update = delete_menu->addAction(tr("Delete Game + &Update"));
    // "Delete Game" now takes the update with it, so this entry would do exactly
    // the same thing. Kept wired up but out of sight; flip this back to
    // has_update to bring it back.
    delete_game_and_update->setVisible(false);
    QAction* delete_save_data = delete_menu->addAction(tr("&Delete Save Data"));
    delete_save_data->setVisible(DeleteTargetExists(delete_paths.save_data));
    QAction* delete_DLC = delete_menu->addAction(tr("&Delete DLC "));
    delete_DLC->setVisible(DeleteTargetExists(delete_paths.dlc));
    QAction* delete_trophy = delete_menu->addAction(tr("&Delete Trophy"));
    const bool has_trophies =
        std::ranges::any_of(delete_paths.trophy_dirs, DeleteTargetExists) ||
        std::ranges::any_of(delete_paths.trophy_user_files, DeleteTargetExists);
    delete_trophy->setVisible(has_trophies);
    QAction* delete_shader_cache = delete_menu->addAction(tr("&Delete Shader Cache"));
    delete_shader_cache->setVisible(DeleteTargetExists(delete_paths.shader_cache_dir) ||
                                    DeleteTargetExists(delete_paths.shader_cache_zip));
    delete_menu->addSeparator();
    QAction* clear_metadata_cache = delete_menu->addAction(tr("Clear &Metadata Cache"));

    // Compatibility
    QMenu* compatibility_menu = addMenu(tr("&Compatibility"));
    QAction* compatibility_view = compatibility_menu->addAction(tr("&View Report"));
    QAction* compatibility_submit = compatibility_menu->addAction(tr("&Submit Report"));
    QAction* compatibility_update = compatibility_menu->addAction(tr("&Update Database"));

    compatibility_view->setVisible(gameinfo->compat.index <=
                                   4); // show only for status Playable to Nothing

    // Copy Menu Actions
    connect(copy_info, &QAction::triggered, frame, [name, serial] {
        QApplication::clipboard()->setText(name % QStringLiteral(" [") % serial %
                                           QStringLiteral("]"));
    });
    connect(copy_name, &QAction::triggered, frame,
            [name] { QApplication::clipboard()->setText(name); });
    connect(copy_serial, &QAction::triggered, frame,
            [serial] { QApplication::clipboard()->setText(serial); });

    // Delete Menu Actions

    connect(delete_game, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::Game); });
    connect(delete_update, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::Update); });
    connect(delete_game_and_update, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::GameAndUpdate); });
    if (gameinfo->has_custom_config) {
        QAction* remove_custom_config = delete_menu->addAction(tr("&Remove Custom Configuration"));
        connect(remove_custom_config, &QAction::triggered, frame, [frame, serial, gameinfo]() {
            if (frame->RemoveCustomConfiguration(serial, gameinfo)) {
                frame->ShowCustomConfigIcon(gameinfo);
            }
        });
    }
    connect(delete_save_data, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::SaveData); });
    connect(delete_DLC, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::DLC); });
    connect(delete_trophy, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::Trophy); });
    connect(delete_shader_cache, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::ShaderCache); });
    connect(clear_metadata_cache, &QAction::triggered, frame,
            [=] { deleteHandler(GameListFrame::DeleteType::MetadataCache); });

    // Compatibility menu actions
    connect(compatibility_view, &QAction::triggered, frame, [frame, gameinfo] {
        if (gameinfo->compat.issue_number != "") {
            QDesktopServices::openUrl(
                QUrl(frame->m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
                     "issues/" + gameinfo->compat.issue_number));
        } else {
            QMessageBox::information(
                frame, tr("No Report Available"),
                tr("There is no compatibility report available for this game."));
        }
    });
    connect(compatibility_submit, &QAction::triggered, frame, [frame, current_game, gameinfo] {
        std::filesystem::path log_file_path =
            (Common::FS::GetUserPath(Common::FS::PathType::LogDir) /
             (frame->m_emu_settings->IsLogSeparate() ? current_game.serial + ".log"
                                                     : "shad_log.txt"));
        bool is_valid_file = LogAnalyzer::ProcessFile(log_file_path);
        std::optional<std::string> report_result = std::nullopt;
        if (is_valid_file) {
            report_result = LogAnalyzer::CheckResults(current_game.serial);
        }
        if ((!is_valid_file || report_result.has_value()) &&
            !frame->m_gui_settings->GetValue(GUI::compatibility_bypass_loganalyzer).toBool()) {
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
                    QUrl(frame->m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
                         "?tab=readme-ov-file#rules"));
            }
            return;
        }
        if (gameinfo->compat.issue_number == "") {
            QUrl url =
                QUrl(frame->m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
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
                frame->m_gui_settings->GetValue(GUI::compatibility_issues_url).toString() +
                "issues/";
            QDesktopServices::openUrl(QUrl(url_issues + gameinfo->compat.issue_number));
        }
    });
    connect(compatibility_update, &QAction::triggered, frame,
            [frame] { frame->m_game_compat->RequestCompatibility(true); });

    // Manage Game menu actions
    connect(convert_to_zar, &QAction::triggered, frame,
            [convertToZArchiveHandler, gameinfo] { convertToZArchiveHandler(gameinfo); });
    connect(
        convert_update_to_zar, &QAction::triggered, frame,
        [convertUpdateToZArchiveHandler, gameinfo] { convertUpdateToZArchiveHandler(gameinfo); });
    connect(convert_from_zar, &QAction::triggered, frame,
            [convertFromZArchiveHandler, gameinfo] { convertFromZArchiveHandler(gameinfo); });
    connect(browse_zar, &QAction::triggered, frame, [frame, gameinfo] {
        auto* dialog = new ZArchiveViewerDialog(std::filesystem::path(gameinfo->info.path), frame);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(browse_update_zar, &QAction::triggered, frame, [frame, gameinfo] {
        auto* dialog =
            new ZArchiveViewerDialog(std::filesystem::path(gameinfo->info.update_path), frame);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(hide_serial, &QAction::triggered, frame, [game_key, frame](bool checked) {
        if (checked)
            frame->m_hidden_list.insert(game_key);
        else
            frame->m_hidden_list.remove(game_key);

        frame->m_gui_settings->SetValue(GUI::game_list_hidden_list,
                                        QStringList(frame->m_hidden_list.values()));
        frame->Refresh();
    });
    connect(edit_notes, &QAction::triggered, frame, [frame, name, serial, game_key, game_path] {
        // fetch old notes from the game info database
        const QString old_notes =
            frame->GetInfoCache() ? frame->GetInfoCache()->GetNotes(game_path) : QString();

        QInputDialog dlg(frame);
        dlg.setWindowTitle(tr("Edit Tooltip Notes"));
        dlg.setLabelText(name + "\n" + serial);
        dlg.setOption(QInputDialog::UsePlainTextEditForTextInput, true);
        dlg.setTextValue(old_notes);
        dlg.setMinimumSize(300, 200);

        if (dlg.exec() == QDialog::Accepted) {
            const QString new_notes = dlg.textValue().trimmed();

            if (new_notes.isEmpty()) {
                frame->m_notes.erase(game_key);
            } else {
                frame->m_notes.insert_or_assign(game_key, new_notes);
            }
            if (frame->GetInfoCache()) {
                frame->GetInfoCache()->SetNotes(game_path, new_notes);
            }

            frame->Refresh();
        }
    });
    connect(edit_title, &QAction::triggered, frame, [frame, name, serial, game_key, game_path] {
        // The name from param.sfo, shown as the placeholder and used to detect
        // "the user typed the original name back in".
        const QString original_title = name;
        const QString old_title =
            frame->GetInfoCache() ? frame->GetInfoCache()->GetTitle(game_path) : QString();

        QInputDialog dlg(frame);
        dlg.setWindowTitle(tr("Rename in Game List"));
        dlg.setLabelText(tr("Shown instead of \"%1\" [%2].\nLeave empty to use the original name.")
                             .arg(original_title, serial));
        dlg.setTextValue(old_title);
        dlg.setMinimumWidth(400);

        if (dlg.exec() != QDialog::Accepted) {
            return;
        }

        QString new_title = dlg.textValue().trimmed();
        // Typing the original name back in is the same as clearing the override.
        if (new_title == original_title) {
            new_title.clear();
        }
        if (new_title == old_title) {
            return;
        }

        if (new_title.isEmpty()) {
            frame->m_titles.erase(game_key);
        } else {
            frame->m_titles.insert_or_assign(game_key, new_title);
        }
        if (frame->GetInfoCache()) {
            frame->GetInfoCache()->SetTitle(game_path, new_title);
        }

        frame->Refresh();
    });
    auto configure_dialog = [frame, current_game, gameinfo](bool create_cfg_from_global_cfg) {
        SettingsDialog dlg(frame->m_gui_settings, frame->m_emu_settings, frame->m_ipc_client, 0,
                           frame, &current_game, create_cfg_from_global_cfg);

        connect(&dlg, &SettingsDialog::EmuSettingsApplied, [frame, gameinfo]() {
            if (!gameinfo->has_custom_config) {
                gameinfo->has_custom_config = true;
                frame->ShowCustomConfigIcon(gameinfo);
            }
        });

        dlg.exec();
    };
    connect(configure, &QAction::triggered, frame,
            [configure_dialog = std::move(configure_dialog)]() { configure_dialog(true); });

    exec(global_pos);
}
