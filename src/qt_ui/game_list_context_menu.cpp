// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
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
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
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
#include "game_list_context_menu.h"
#include "game_list_frame.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "localized.h"
#include "npbind_dialog.h"
#include "persistent_settings.h"
#include "progress_dialog.h"
#include "qt_utils.h"

#include "cheats_patches_dialog.h"
#include "common/log_analyzer.h"
#include "common/path_util.h"
#include "core/ipc/ipc_client.h"
#include "settings_dialog.h"
#include "sfo_viewer_dialog.h"
#include "trophy_viewer.h"
#include "zarchive_viewer_dialog.h"

GameListContextMenu::GameListContextMenu(GameListFrame* frame) : QMenu(frame), m_frame(frame) {}

void GameListContextMenu::Show(const game_info& gameinfo, const QPoint& global_pos) {
    GameListFrame* frame = m_frame;

    auto deleteHandler = [frame, gameinfo](GameListFrame::DeleteType type) {
        bool error = false;
        QString folder_path;
        QString message_type;

        QString game_path;
        Common::FS::PathToQString(game_path, gameinfo->info.path);

        // gameinfo->info.update_path is resolved at scan time and already
        // accounts for the base game (or the update/patch itself) being a
        // .zar archive rather than a plain folder.
        QString update_path;
        Common::FS::PathToQString(update_path, gameinfo->info.update_path);

        QString dlc_path;
        Common::FS::PathToQString(
            dlc_path, frame->m_emu_settings->GetAddonInstallDir() /
                          Common::FS::PathFromQString(game_path).parent_path().filename());

        std::string default_user_id =
            std::to_string(UserSettings.GetUserManager().GetDefaultUser().user_id);
        QString save_data_path;
        Common::FS::PathToQString(save_data_path, EmulatorSettings.GetHomeDir() / default_user_id /
                                                      "savedata" / gameinfo->info.save_dir);

        // QString trophy_path;
        // Common::FS::PathToQString(trophy_path,
        //                           Common::FS::GetUserPath(Common::FS::PathType::MetaDataDir)
        //                           /
        //                               gameinfo->info.serial / "TrophyFiles");

        switch (type) {
        case GameListFrame::DeleteType::Game:
            BackgroundMusicPlayer::getInstance().StopMusic();
            folder_path = game_path;
            message_type = tr("Game");
            break;

        case GameListFrame::DeleteType::Update:
            if (!std::filesystem::exists(Common::FS::PathFromQString(update_path))) {
                QMessageBox::critical(frame, tr("Error"), tr("This game has no update to delete!"));
                return;
            }
            folder_path = update_path;
            message_type = tr("Update");
            break;

        case GameListFrame::DeleteType::GameAndUpdate: {
            if (update_path.isEmpty() ||
                !std::filesystem::exists(Common::FS::PathFromQString(update_path))) {
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

            BackgroundMusicPlayer::getInstance().StopMusic();

            auto remove_path = [](const QString& path) {
                const std::filesystem::path path_to_delete = Common::FS::PathFromQString(path);
                std::error_code remove_ec;
                if (std::filesystem::is_regular_file(path_to_delete, remove_ec)) {
                    // A ZArchive-packed game/update is a single file, not a
                    // directory; QDir::removeRecursively() would silently
                    // do nothing for it.
                    std::filesystem::remove(path_to_delete, remove_ec);
                } else {
                    QDir(path).removeRecursively();
                }
            };

            // Delete the update first: if it were interrupted partway, we'd
            // rather be left with just the (still-launchable) base game
            // than an orphaned update pointing at a deleted base.
            remove_path(update_path);
            remove_path(game_path);

            frame->Refresh(true);
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

        case GameListFrame::DeleteType::Trophy:
            //     if (!std::filesystem::exists(Common::FS::PathFromQString(trophy_path))) {
            //         QMessageBox::critical(frame, tr("Error"),
            //                               tr("This game has no saved trophies to delete!"));
            //         return;
            //     }
            //     folder_path = trophy_path;
            //     message_type = tr("Trophy");
            break;

        case GameListFrame::DeleteType::ShaderCache: {
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

        QMessageBox::StandardButton reply = QMessageBox::question(
            frame, tr("Delete %1").arg(message_type),
            tr("Are you sure you want to delete %1's %2 directory?")
                .arg(QString::fromStdString(gameinfo->info.name), message_type),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            const std::filesystem::path path_to_delete = Common::FS::PathFromQString(folder_path);
            std::error_code remove_ec;
            if (std::filesystem::is_regular_file(path_to_delete, remove_ec)) {
                // A ZArchive-packed game/update is a single file, not a
                // directory; QDir::removeRecursively() would silently do
                // nothing for it.
                std::filesystem::remove(path_to_delete, remove_ec);
            } else {
                QDir(folder_path).removeRecursively();
            }

            if (type == GameListFrame::DeleteType::Game) {
                frame->Refresh(true);
            }
        }
    };

    // Packs an arbitrary folder (either a base game or its associated
    // update/patch folder) into a .zar archive. Shared by
    // convertToZArchiveHandler and convertUpdateToZArchiveHandler below.
    auto convertPathToZArchiveHandler = [frame](const std::filesystem::path& source_path,
                                                const QString& display_name,
                                                const QString& dialog_title,
                                                const QString& extra_note) {
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
        Common::FS::PathToQString(default_output, source_path.parent_path() /
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

        auto* progress = new ProgressDialog(dialog_title, tr("Packing %1...").arg(display_name),
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
                [frame, watcher, progress_guard, source_path, output_path, dialog_title]() {
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

                    frame->Refresh(true);
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

    auto convertToZArchiveHandler = [frame, convertPathToZArchiveHandler](const game_info& game) {
        const std::filesystem::path source_path(game->info.path);
        const QString game_name = QString::fromStdString(game->info.name);

        QString extra_note;
        if (!game->info.update_path.empty() && !Core::FileSys::IsZArchiveFile(source_path)) {
            extra_note = tr("\n\nThis game has a separate update/patch folder. Only the base game "
                            "will be archived; the update/patch folder will not be included and "
                            "will be left as-is. Use \"Convert Update to ZArchive\" separately if "
                            "you'd like to archive it too.");
        }

        convertPathToZArchiveHandler(source_path, game_name, tr("Convert to ZArchive"), extra_note);
    };

    auto convertUpdateToZArchiveHandler = [frame,
                                           convertPathToZArchiveHandler](const game_info& game) {
        if (game->info.update_path.empty()) {
            QMessageBox::information(
                frame, tr("Convert Update to ZArchive"),
                tr("This game has no separate update/patch folder to archive."));
            return;
        }

        const std::filesystem::path source_path(game->info.update_path);
        const QString display_name = tr("%1 Update").arg(QString::fromStdString(game->info.name));

        convertPathToZArchiveHandler(source_path, display_name, tr("Convert Update to ZArchive"),
                                     QString());
    };

    auto convertFromZArchiveHandler = [frame](const game_info& game) {
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
        // If the user picked an existing empty/foreign folder directly, extract there;
        // otherwise nest under a folder named after the archive to avoid clobbering.
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
                [frame, watcher, progress_guard, source_path, output_path]() {
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

                    frame->Refresh(true);
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
            // update_path may itself be a .zar archive, so resolve through
            // the game backend rather than concatenating a raw path.
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

        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(fs_path.string())));
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
            // Directories resolve to the file itself; a .zar-packed
            // base/update resolves to an extracted copy in the cache dir.
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

        // current_game.update_path is resolved at scan time and is already
        // aware of .zar-packed base/update folders.
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
    hide_serial->setChecked(frame->m_hidden_list.contains(serial));
    QAction* edit_notes = manage_game_menu->addAction(tr("&Add/Edit Tooltip Notes"));
    QAction* convert_to_zar = manage_game_menu->addAction(tr("&Convert to ZArchive (.zar)..."));
    convert_to_zar->setEnabled(
        !Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.path)));
    QAction* convert_update_to_zar =
        manage_game_menu->addAction(tr("Convert &Update to ZArchive (.zar)..."));
    convert_update_to_zar->setEnabled(
        !current_game.update_path.empty() &&
        !Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.update_path)));
    QAction* convert_from_zar = manage_game_menu->addAction(tr("&Extract from ZArchive (.zar)..."));
    convert_from_zar->setEnabled(
        Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.path)));
    QAction* browse_zar = manage_game_menu->addAction(tr("&Browse ZArchive Contents..."));
    browse_zar->setEnabled(Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.path)));
    QAction* browse_update_zar =
        manage_game_menu->addAction(tr("Browse &Update ZArchive Contents..."));
    browse_update_zar->setEnabled(
        !current_game.update_path.empty() &&
        Core::FileSys::IsZArchiveFile(std::filesystem::path(current_game.update_path)));

    // Copy Info menu
    QMenu* info_menu = addMenu(tr("&Copy Info"));
    QAction* copy_info = info_menu->addAction(tr("&Copy Name + Serial"));
    QAction* copy_name = info_menu->addAction(tr("&Copy Name"));
    QAction* copy_serial = info_menu->addAction(tr("&Copy Serial"));

    // Delete
    QMenu* delete_menu = addMenu(tr("&Delete..."));
    QAction* delete_game = delete_menu->addAction(tr("&Delete Game"));
    QAction* delete_update = delete_menu->addAction(tr("&Delete Update"));
    QAction* delete_game_and_update = delete_menu->addAction(tr("Delete Game + &Update"));
    delete_game_and_update->setEnabled(!current_game.update_path.empty());
    QAction* delete_save_data = delete_menu->addAction(tr("&Delete Save Data"));
    QAction* delete_DLC = delete_menu->addAction(tr("&Delete DLC "));
    QAction* delete_trophy = delete_menu->addAction(tr("&Delete Trophy"));
    QAction* delete_shader_cache = delete_menu->addAction(tr("&Delete Shader Cache"));
    delete_menu->addSeparator();
    QAction* clear_metadata_cache = delete_menu->addAction(tr("Clear &Metadata Cache"));
    delete_trophy->setEnabled(false); // TODO

    // Compatibility
    QMenu* compatibility_menu = addMenu(tr("&Compatibility"));
    QAction* compatibility_view = compatibility_menu->addAction(tr("&View Report"));
    QAction* compatibility_submit = compatibility_menu->addAction(tr("&Submit Report"));
    QAction* compatibility_update = compatibility_menu->addAction(tr("&Update Database"));

    compatibility_view->setEnabled(gameinfo->compat.index <=
                                   4); // enable for status Playable to Nothing

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
    connect(hide_serial, &QAction::triggered, frame, [serial, frame](bool checked) {
        if (checked)
            frame->m_hidden_list.insert(serial);
        else
            frame->m_hidden_list.remove(serial);

        frame->m_gui_settings->SetValue(GUI::game_list_hidden_list,
                                        QStringList(frame->m_hidden_list.values()));
        frame->Refresh();
    });
    connect(edit_notes, &QAction::triggered, frame, [frame, name, serial] {
        bool accepted;
        // fetch old notes from persistent storage
        const QString old_notes =
            frame->m_persistent_settings->GetValue(GUI::Persistent::notes, serial, "").toString();

        QInputDialog dlg(frame);
        dlg.setWindowTitle(tr("Edit Tooltip Notes"));
        dlg.setLabelText(name + "\n" + serial);
        dlg.setOption(QInputDialog::UsePlainTextEditForTextInput, true);
        dlg.setTextValue(old_notes);
        dlg.setMinimumSize(300, 200);

        if (dlg.exec() == QDialog::Accepted) {
            const QString new_notes = dlg.textValue().trimmed();

            if (new_notes.isEmpty()) {
                frame->m_notes.erase(serial);
                frame->m_persistent_settings->RemoveValue(GUI::Persistent::notes, serial);
            } else {
                frame->m_notes.insert_or_assign(serial, new_notes);
                frame->m_persistent_settings->SetValue(GUI::Persistent::notes, serial, new_notes);
            }

            frame->Refresh();
        }
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
