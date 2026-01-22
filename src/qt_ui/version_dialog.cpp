// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <common/path_util.h>
#include <common/versions.h>
#include <common/zip_util.h>
#include "gui_settings.h"
#include "qt_ui/main_window.h"
#include "qt_utils.h"
#include "ui_version_dialog.h"
#include "version_dialog.h"

VersionDialog::VersionDialog(std::shared_ptr<GUISettings> gui_settings, QWidget* parent)
    : QDialog(parent), ui(new Ui::VersionDialog), m_gui_settings(std::move(gui_settings)) {
    ui->setupUi(this);
    this->setMinimumSize(670, 350);

    auto const& version_list = VersionManager::GetVersionList();

    m_downloader = new Downloader(m_gui_settings, std::nullopt, std::nullopt, this);

    ui->checkOnStartupCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::version_manager_checkOnStartup).toBool());
    ui->showChangelogCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::version_manager_showChangeLog).toBool());

    connect(ui->checkOnStartupCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_gui_settings->SetValue(GUI::version_manager_checkOnStartup, checked);
    });
    connect(ui->showChangelogCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_gui_settings->SetValue(GUI::version_manager_showChangeLog, checked);
    });

    connect(this, &VersionDialog::WindowResized, this, &VersionDialog::HandleResize);

    networkManager = new QNetworkAccessManager(this);

    if (m_gui_settings->GetValue(GUI::version_manager_versionPath).toString() == "") {
        QString versionDir = QString::fromStdString(
            GUI::Utils::NormalizePath(Common::FS::GetUserPath(Common::FS::PathType::VersionDir)));
        QDir dir(versionDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        m_gui_settings->SetValue(GUI::version_manager_versionPath, versionDir);
    }

    ui->currentVersionPath->setText(
        m_gui_settings->GetValue(GUI::version_manager_versionPath).toString());

    LoadInstalledList();

    QString cachePath = QDir(m_gui_settings->GetValue(GUI::version_manager_versionPath).toString())
                            .filePath("cache.json");
    if (QFile::exists(cachePath)) {
        PopulateDownloadTree();
    } else {
        DownloadListVersion();
    }

    connect(ui->browse_versionPath, &QPushButton::clicked, this, [this]() {
        const auto shad_exe_path =
            m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();
        QString initial_path = shad_exe_path;

        QString shad_folder_path_string = QFileDialog::getExistingDirectory(
            this, tr("Select the folder where the emulator versions will be installed"),
            initial_path);

        auto folder_path = Common::FS::PathFromQString(shad_folder_path_string);
        if (!folder_path.empty()) {
            ui->currentVersionPath->setText(shad_folder_path_string);
            m_gui_settings->SetValue(GUI::version_manager_versionPath, shad_folder_path_string);
            m_gui_settings->SetValue(GUI::version_manager_versionSelected, "");
            LoadInstalledList();
        }
    });

    connect(ui->checkChangesVersionButton, &QPushButton::clicked, this,
            [this]() { LoadInstalledList(); });

    connect(ui->addCustomVersionButton, &QPushButton::clicked, this, [this, version_list]() {
        QString exePath;

#ifdef Q_OS_WIN
        exePath = QFileDialog::getOpenFileName(this, tr("Select executable"), QDir::rootPath(),
                                               tr("Executable (*.exe)"));
#elif defined(Q_OS_LINUX)
    exePath = QFileDialog::getOpenFileName(this, tr("Select executable"), QDir::rootPath(),
                                           tr("Executable (*)"));
#elif defined(Q_OS_MACOS)
    exePath = QFileDialog::getOpenFileName(this, tr("Select executable"), QDir::rootPath(),
                                           tr("Executable (*.*)"));
#endif

        if (exePath.isEmpty())
            return;

        bool ok;
        QString version_name =
            QInputDialog::getText(this, tr("Version name"),
                                  tr("Enter the name of this version as it appears in the list."),
                                  QLineEdit::Normal, "", &ok);

        if (!ok || version_name.trimmed().isEmpty())
            return;

        version_name = version_name.trimmed();

        if (std::find_if(version_list.cbegin(), version_list.cend(), [&](const auto& v) {
                return v.name == version_name.toStdString();
            }) != version_list.cend()) {
            QMessageBox::warning(this, tr("Error"), tr("A version with that name already exists."));
            return;
        }

        QString versionsRoot =
            m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();

        if (versionsRoot.isEmpty()) {
            QMessageBox::warning(this, tr("Error"), tr("Version install path is not configured."));
            return;
        }

        QString destFolder = QDir(versionsRoot).filePath(version_name);
        QDir().mkpath(destFolder);

        QFileInfo srcInfo(exePath);
        QString destExePath = QDir(destFolder).filePath(srcInfo.fileName());

        if (QFile::exists(destExePath))
            QFile::remove(destExePath);

        if (!QFile::copy(exePath, destExePath)) {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to copy executable into versions folder."));
            return;
        }

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QFile(destExePath)
            .setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                            QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther |
                            QFile::ExeOther);
#endif

        // ---- Copy to application directory (same as releases) ----

        QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
        QString appExePath = appDir + "/shadPS4.exe";
#elif defined(Q_OS_LINUX)
        QString appExePath = appDir + "/shadPS4";
#elif defined(Q_OS_MACOS)
        QString appExePath = appDir + "/shadPS4.app/Contents/MacOS/shadPS4";
#endif

        if (QFile::exists(appExePath)) {
            QString backupPath = appExePath + ".backup";
            if (QFile::exists(backupPath))
                QFile::remove(backupPath);
            QFile::rename(appExePath, backupPath);
        }

        if (!QFile::copy(destExePath, appExePath)) {
            QMessageBox::warning(this, tr("Warning"),
                                 tr("Failed to install executable into application directory.\n"
                                    "The custom build is still available under:\n%1")
                                     .arg(destFolder));
        } else {
#if defined(Q_OS_LINUX)
            QFile(appExePath)
                .setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther |
                                QFile::ExeOther);
#endif
        }

        VersionManager::Version new_version{
            .name = version_name.toStdString(),
            .path = destExePath.toStdString(), // IMPORTANT: version folder path
            .date = QLocale::system()
                        .toString(QDate::currentDate(), QLocale::ShortFormat)
                        .toStdString(),
            .codename = tr("Local").toStdString(),
            .type = VersionManager::VersionType::Custom,
        };

        VersionManager::AddNewVersion(new_version);

        m_gui_settings->SetValue(GUI::version_manager_versionSelected, destExePath);

        QMessageBox::information(this, tr("Success"),
                                 tr("Custom version installed successfully:") + ("\n\n ") +
                                     tr("1. Version folder:") +
                                     QString(" %1\n\n ").arg(destFolder) + tr("2. Installed to:") +
                                     QString(" %1").arg(appExePath));
        LoadInstalledList();
    });

    connect(ui->deleteVersionButton, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* selectedItem = ui->installedTreeWidget->currentItem();
        if (!selectedItem) {
            QMessageBox::warning(
                this, tr("Error"),
                tr("No version selected. Please choose one from the list to delete."));
            return;
        }

        QString versionName = selectedItem->text(1);
        QString fullPath = selectedItem->text(4);

        if (fullPath.isEmpty()) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to determine the folder path."));
            return;
        }
        auto reply = QMessageBox::question(this, tr("Delete version"),
                                           tr("Do you want to delete the version") +
                                               QString(" \"%1\" ?").arg(versionName),
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No)
            return;

        // Check if it is of type Local (type == 2)
        auto versions = VersionManager::GetVersionList({});
        int versionType = 2;
        for (const auto& v : versions) {
            if (v.name == versionName.toStdString()) {
                versionType = static_cast<int>(v.type);
                break;
            }
        }

        if (versionType == 2) {
            VersionManager::RemoveVersion(versionName.toStdString());
            LoadInstalledList();
            return;
        }

        QFileInfo info(fullPath);
        QString folderPath;
        if (info.exists() && info.isDir()) {
            folderPath = info.absoluteFilePath();
        } else {
            folderPath = info.absolutePath();
        }

        if (folderPath.isEmpty()) {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to determine the folder to remove.") +
                                      QString("\n \"%1\"").arg(fullPath));
            return;
        }

        QDir dirToRemove(folderPath);
        if (dirToRemove.exists()) {
            if (!dirToRemove.removeRecursively()) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to delete folder.") +
                                          QString("\n \"%1\"").arg(folderPath));
                return;
            }
        }

        VersionManager::RemoveVersion(versionName.toStdString());
        LoadInstalledList();
    });

    connect(ui->checkVersionDownloadButton, &QPushButton::clicked, this,
            [this]() { DownloadListVersion(); });

    connect(ui->installedTreeWidget, &QTreeWidget::itemChanged, this,
            &VersionDialog::onItemChanged);

    connect(ui->updatePreButton, &QPushButton::clicked, this, [this]() { checkUpdatePre(true); });
};

VersionDialog::~VersionDialog() {
    delete ui;
}

void VersionDialog::resizeEvent(QResizeEvent* event) {
    emit WindowResized(event);
    QDialog::resizeEvent(event);
}

void VersionDialog::HandleResize(QResizeEvent* event) {
    this->ui->versionTab->resize(this->size());
}

bool VersionDialog::CopyExecutableToAppDir(const QString& sourceExe, QWidget* parent) {
    if (sourceExe.isEmpty() || !QFile::exists(sourceExe)) {
        QMessageBox::warning(parent, QObject::tr("Error"),
                             QObject::tr("Executable does not exist:\n%1").arg(sourceExe));
        return false;
    }

    QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    QString appExePath = appDir + "/shadPS4.exe";
#elif defined(Q_OS_LINUX)
    QString appExePath = appDir + "/shadPS4";
#elif defined(Q_OS_MACOS)
    QString appExePath = appDir + "/shadPS4.app/Contents/MacOS/shadPS4";
#endif

    if (QFile::exists(appExePath)) {
        QString backupPath = appExePath + ".backup";
        QFile::remove(backupPath);
        QFile::rename(appExePath, backupPath);
    }

    if (!QFile::copy(sourceExe, appExePath)) {
        QMessageBox::warning(parent, QObject::tr("Error"),
                             QObject::tr("Failed to copy executable to application directory."));
        return false;
    }

#if defined(Q_OS_LINUX)
    QFile(appExePath)
        .setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                        QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
#endif

    return true;
}

void VersionDialog::onItemChanged(QTreeWidgetItem* item, int column) {
    if (column != 0)
        return;

    if (item->checkState(0) != Qt::Checked) {
        item->setSelected(false);
        return;
    }

    // Uncheck all other items
    for (int row = 0; row < ui->installedTreeWidget->topLevelItemCount(); ++row) {
        QTreeWidgetItem* topItem = ui->installedTreeWidget->topLevelItem(row);
        if (topItem != item) {
            topItem->setCheckState(0, Qt::Unchecked);
            topItem->setSelected(false);
        }
    }

    QString versionExePath = item->text(4); // stored executable path

    if (!CopyExecutableToAppDir(versionExePath, this)) {
        // Roll back checkbox if install failed
        item->setCheckState(0, Qt::Unchecked);
        return;
    }

    m_gui_settings->SetValue(GUI::version_manager_versionSelected, versionExePath);
    item->setSelected(true);
}

void VersionDialog::DownloadListVersion() {
    QString downloadUrl = "https://api.github.com/repos/shadps4-emu/shadPS4/tags";
    QString cachePath = QDir(m_gui_settings->GetValue(GUI::version_manager_versionPath).toString())
                            .filePath("cache.json");

    disconnect(m_downloader, nullptr, this, nullptr);
    connect(m_downloader, &Downloader::SignalDownloadFinished, this,
            [this]() { PopulateDownloadTree(); });

    m_downloader->DownloadJSONWithCache(downloadUrl.toStdString(), cachePath, true,
                                        tr("Checking for new emulator versions..."), 2000);
}

void VersionDialog::InstallSelectedVersion() {
    disconnect(ui->downloadTreeWidget, &QTreeWidget::itemClicked, nullptr, nullptr);

    connect(
        ui->downloadTreeWidget, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (m_gui_settings->GetValue(GUI::version_manager_versionPath).toString().isEmpty()) {
                QMessageBox::warning(
                    this, tr("Select the folder where the emulator versions will be installed"),
                    // clang-format off
tr("First you need to choose a location to save the versions in\n'Path to save versions'"));
                // clang-format on
                return;
            }
            QString versionName = item->text(0);
            QString apiUrl;
            QString platform;

#ifdef Q_OS_WIN
            platform = "win64-sdl";
#elif defined(Q_OS_LINUX)
            platform = "linux-sdl";
#elif defined(Q_OS_MACOS)
            platform = "macos-sdl";
#endif
            if (versionName.contains("Pre-release", Qt::CaseInsensitive)) {
                apiUrl = "https://api.github.com/repos/shadps4-emu/shadPS4/releases";
            } else {
                apiUrl =
                    QString("https://api.github.com/repos/shadps4-emu/shadPS4/releases/tags/%1")
                        .arg(versionName);
            }

            // Confirm download
            if (QMessageBox::question(this, tr("Confirm Download"),
                                      tr("Do you want to download the version") +
                                          QString(": %1 ?").arg(versionName),
                                      QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                return;
            }

            QNetworkAccessManager* manager = new QNetworkAccessManager(this);
            QNetworkRequest request(apiUrl);
            QNetworkReply* reply = manager->get(request);

            connect(reply, &QNetworkReply::finished, this, [this, reply, platform, versionName]() {
                if (reply->error() != QNetworkReply::NoError) {
                    QMessageBox::warning(this, tr("Error"), reply->errorString());
                    reply->deleteLater();
                    return;
                }

                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonObject release;
                QJsonArray assets;

                if (versionName.contains("Pre-release", Qt::CaseInsensitive)) {
                    for (const auto& val : doc.array()) {
                        QJsonObject obj = val.toObject();
                        if (obj["prerelease"].toBool()) {
                            release = obj;
                            assets = obj["assets"].toArray();
                            break;
                        }
                    }
                } else {
                    release = doc.object();
                    assets = release["assets"].toArray();
                }

                QString downloadUrl;
                for (const QJsonValue& val : assets) {
                    QJsonObject obj = val.toObject();
                    if (obj["name"].toString().contains(platform)) {
                        downloadUrl = obj["browser_download_url"].toString();
                        break;
                    }
                }
                if (downloadUrl.isEmpty()) {
                    QMessageBox::warning(this, tr("Error"),
                                         tr("No files available for this platform."));
                    reply->deleteLater();
                    return;
                }

                QString userPath =
                    m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();
                QString zipPath = QDir(userPath).filePath("temp_download_update.zip");

                disconnect(m_downloader, nullptr, this, nullptr);
                m_downloader->DownloadJSONWithCache(downloadUrl.toStdString(), zipPath, true,
                                                    tr("Downloading") + " " + versionName, 0);

                connect(m_downloader, &Downloader::SignalDownloadError, this,
                        [this](const QString& err) {
                            QMessageBox::warning(this, tr("Error"),
                                                 tr("Error accessing GitHub") + ":\n" + err);
                        });

                connect(
                    m_downloader, &Downloader::SignalDownloadFinished, this,
                    [this, release, versionName, userPath, zipPath](const QByteArray&) {
                        QString normalizedVersionName = versionName;
                        // Normalize version format: convert "v.0.11.0" to "v0.11.0"
                        // Also handle other possible formats
                        if (normalizedVersionName.startsWith("v.")) {
                            normalizedVersionName = "v" + normalizedVersionName.mid(2);
                        } else if (normalizedVersionName.startsWith("version ",
                                                                    Qt::CaseInsensitive)) {
                            normalizedVersionName = "v" + normalizedVersionName.mid(8);
                        } else if (normalizedVersionName.startsWith("Version ",
                                                                    Qt::CaseInsensitive)) {
                            normalizedVersionName = "v" + normalizedVersionName.mid(8);
                        } else if (!normalizedVersionName.startsWith("v")) {
                            // If it doesn't start with v at all, add it (for cases like "0.11.0")
                            normalizedVersionName = "v" + normalizedVersionName;
                        }

                        QString folderName =
                            versionName.contains("Pre-release", Qt::CaseInsensitive)
                                ? "Pre-release"
                                : normalizedVersionName;

                        QString destFolder = QDir(userPath).filePath(folderName);
                        QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
                        QString appExePath = appDir + "/shadPS4.exe";
#elif defined(Q_OS_LINUX)
                            QString appExePath = appDir + "/shadPS4";
#elif defined(Q_OS_MACOS)
                            QString appExePath = appDir + "/shadPS4.app/Contents/MacOS/shadPS4";
#endif

                        // extract ZIP to version folder
                        try {
                            Zip::Extract(zipPath, destFolder);
                            QFile::remove(zipPath);
                        } catch (const std::exception& e) {
                            QMessageBox::critical(this, tr("Error"),
                                                  tr("ZIP extraction failed:") + ("\n") + e.what());
                            return;
                        }

                        // Find the executable in the extracted folder
                        QString versionExePath;
                        QDirIterator it(destFolder, QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            it.next();
                            QFileInfo fi = it.fileInfo();
#ifdef Q_OS_WIN
                            if (fi.isFile() &&
                                fi.suffix().compare("exe", Qt::CaseInsensitive) == 0) {
                                versionExePath = fi.absoluteFilePath();
                                break;
                            }
#elif defined(Q_OS_LINUX)
                                if (fi.isFile() && fi.isExecutable() &&
                                    !fi.fileName().contains('.')) {
                                    versionExePath = fi.absoluteFilePath();
                                    break;
                                }
#elif defined(Q_OS_MACOS)
                                if (fi.fileName() == "shadPS4" && fi.isExecutable()) {
                                    versionExePath = fi.absoluteFilePath();
                                    break;
                                }
#endif
                        }

                        if (versionExePath.isEmpty()) {
                            QMessageBox::warning(this, tr("Error"),
                                                 tr("Executable not found in extracted files."));
                            return;
                        }

                        // Copy to application directory
                        bool copySuccess = false;

                        if (QFile::exists(appExePath)) {
                            // Create backup of old executable
                            QString backupPath = appExePath + ".backup";
                            if (QFile::exists(backupPath)) {
                                QFile::remove(backupPath);
                            }
                            QFile::rename(appExePath, backupPath);
                        }

                        if (QFile::copy(versionExePath, appExePath)) {
                            copySuccess = true;
#ifdef Q_OS_LINUX
                            // Set executable permissions
                            QFile(appExePath)
                                .setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                                QFile::ExeOwner | QFile::ReadGroup |
                                                QFile::ExeGroup | QFile::ReadOther |
                                                QFile::ExeOther);
#endif
                        }

                        if (!copySuccess) {
                            QMessageBox::warning(this, tr("Error"),
                                                 // clang-format off
tr("Failed to copy executable to application directory.\nThe version has been saved to: %1").arg(destFolder));
                                                     // clang-format on                                                     
                        } else {
                            QMessageBox::information(
                                this, tr("Success"),
                                tr("Version %1 has been:").arg(versionName) + ("\n\n ") +
                                    tr("1. Downloaded to:") + QString(" %1\n\n ").arg(destFolder) +
                                    tr("2. Installed to:") + QString(" %1").arg(appExePath));
                        }

                        // Register the version
                        bool is_release = !versionName.contains("Pre-release");
                        QString code_name;
                        if (is_release) {
                            QString rn = release["name"].toString();
                            int idx = rn.indexOf(" - codename ");
                            if (idx != -1)
                                code_name = rn.mid(idx + 12);
                        } else {
                            QRegularExpression re("-([a-fA-F0-9]{7,})$");
                            auto m = re.match(release["tag_name"].toString());
                            code_name = m.hasMatch() ? m.captured(1) : "unknown";
                        }

                        VersionManager::Version v{
                            .name =
                                is_release ? versionName.toStdString() : "Pre-release (Nightly)",
                            .path = Common::FS::PathFromQString(versionExePath).generic_string(),
                            .date =
                                QLocale::system()
                                    .toString(QDateTime::fromString(
                                                  release["published_at"].toString(), Qt::ISODate)
                                                  .date(),
                                              QLocale::ShortFormat)
                                    .toStdString(),
                            .codename = code_name.toStdString(),
                            .type = is_release ? VersionManager::VersionType::Release
                                               : VersionManager::VersionType::Nightly};

                        if (!is_release) {
                            for (const auto& iv : VersionManager::GetVersionList({})) {
                                if (iv.type == VersionManager::VersionType::Nightly)
                                    VersionManager::RemoveVersion(iv.name);
                            }
                        }

                        m_gui_settings->SetValue(GUI::version_manager_versionSelected,
                                                 QString::fromStdString(v.path));

                        VersionManager::AddNewVersion(v);
                        LoadInstalledList();
                    });

                reply->deleteLater();
            });
        });
}

void VersionDialog::LoadInstalledList() {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "versions.json";
    auto versions = VersionManager::GetVersionList(path);
    const auto& selected_version =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString().toStdString();

    std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) {
        auto getOrder = [](int type) {
            switch (type) {
            case 1: // Pre-release
                return 0;
            case 0: // Release
                return 1;
            case 2: // Local
                return 2;
            default:
                return 3;
            }
        };

        int orderA = getOrder(static_cast<int>(a.type));
        int orderB = getOrder(static_cast<int>(b.type));

        if (orderA != orderB)
            return orderA < orderB;

        if (a.type == VersionManager::VersionType::Release) {
            static QRegularExpression versionRegex("^v\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$");
            QRegularExpressionMatch matchA = versionRegex.match(QString::fromStdString(a.name));
            QRegularExpressionMatch matchB = versionRegex.match(QString::fromStdString(b.name));

            if (matchA.hasMatch() && matchB.hasMatch()) {
                int majorA = matchA.captured(1).toInt();
                int minorA = matchA.captured(2).toInt();
                int patchA = matchA.captured(3).toInt();
                int majorB = matchB.captured(1).toInt();
                int minorB = matchB.captured(2).toInt();
                int patchB = matchB.captured(3).toInt();

                if (majorA != majorB)
                    return majorA > majorB;
                if (minorA != minorB)
                    return minorA > minorB;
                return patchA > patchB;
            }
        }

        return QString::fromStdString(a.name).compare(QString::fromStdString(b.name),
                                                      Qt::CaseInsensitive) < 0;
    });

    ui->installedTreeWidget->clear();
    ui->installedTreeWidget->setColumnCount(5);
    ui->installedTreeWidget->setColumnHidden(4, true);

    for (const auto& v : versions) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->installedTreeWidget);
        item->setText(1, QString::fromStdString(v.name));

        QString codename = QString::fromStdString(v.codename);
        if (v.type == VersionManager::VersionType::Nightly) {
            if (codename.length() > 7) {
                codename = codename.left(7);
            }
        }
        item->setText(2, codename);

        item->setText(3, QString::fromStdString(v.date));
        item->setText(4, QString::fromStdString(v.path));
        item->setCheckState(0, (selected_version == v.path) ? Qt::Checked : Qt::Unchecked);
    }

    ui->installedTreeWidget->resizeColumnToContents(0);
    ui->installedTreeWidget->resizeColumnToContents(1);
    ui->installedTreeWidget->resizeColumnToContents(2);
    ui->installedTreeWidget->setColumnWidth(1, ui->installedTreeWidget->columnWidth(1) + 10);
    ui->installedTreeWidget->setColumnWidth(2, ui->installedTreeWidget->columnWidth(2) + 20);
}

QStringList VersionDialog::LoadDownloadCache() {
    QString cachePath = QDir(m_gui_settings->GetValue(GUI::version_manager_versionPath).toString())
                            .filePath("cache.version");
    QStringList cachedVersions;
    QFile file(cachePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd())
            cachedVersions.append(in.readLine().trimmed());
    }
    return cachedVersions;
}

void VersionDialog::PopulateDownloadTree() {
    ui->downloadTreeWidget->clear();

    QString cachePath = QDir(m_gui_settings->GetValue(GUI::version_manager_versionPath).toString())
                            .filePath("cache.json");

    QFile file(cachePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot open cache file"));
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        QMessageBox::warning(this, tr("Error"), tr("Cache file is corrupted"));
        QFile::remove(cachePath);
        return;
    }

    QJsonArray tags = doc.array();
    QTreeWidgetItem* preReleaseItem = nullptr;
    QList<QTreeWidgetItem*> otherItems;
    bool foundPreRelease = false;

    auto isVersionGreaterThan_0_5_0 = [](const QString& tagName) -> bool {
        QRegularExpression versionRegex(R"(v\.?(\d+)\.(\d+)\.(\d+))");
        QRegularExpressionMatch match = versionRegex.match(tagName);
        if (match.hasMatch()) {
            int major = match.captured(1).toInt();
            int minor = match.captured(2).toInt();
            if (major > 0)
                return true;
            if (major == 0 && minor >= 5)
                return true;
        }
        return false;
    };

    for (const QJsonValue& value : tags) {
        QString tagName = value.toObject()["name"].toString();

        if (tagName.startsWith("Pre-release", Qt::CaseInsensitive)) {
            if (!foundPreRelease) {
                preReleaseItem = new QTreeWidgetItem();
                preReleaseItem->setText(0, "Pre-release (Nightly)");
                foundPreRelease = true;
            }
            continue;
        }

        if (isVersionGreaterThan_0_5_0(tagName)) {
            QTreeWidgetItem* item = new QTreeWidgetItem();
            item->setText(0, tagName);
            otherItems.append(item);
        }
    }

    if (!foundPreRelease) {
        preReleaseItem = new QTreeWidgetItem();
        preReleaseItem->setText(0, "Pre-release (Nightly)");
    }

    if (preReleaseItem)
        ui->downloadTreeWidget->addTopLevelItem(preReleaseItem);
    for (QTreeWidgetItem* item : otherItems)
        ui->downloadTreeWidget->addTopLevelItem(item);

    InstallSelectedVersion();
}

void VersionDialog::checkUpdatePre(const bool showMessage) {
    QString versionPath = m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();
    if (versionPath.isEmpty() || !QDir(versionPath).exists()) {
        return;
    }

    auto versions = VersionManager::GetVersionList({});

    QString localHash;
    QString localFolderPath;
    QString localTag;

    for (const auto& v : versions) {
        if (v.type == VersionManager::VersionType::Nightly) {
            localHash = QString::fromStdString(v.codename);
            localFolderPath = QString::fromStdString(v.path);
            localTag = QString::fromStdString(v.name);
            break;
        }
    }

    if (localHash.isEmpty()) {
        auto* tree = ui->downloadTreeWidget;
        int topCount = tree->topLevelItemCount();

        for (int i = 0; i < topCount; ++i) {
            QTreeWidgetItem* item = tree->topLevelItem(i);
            if (item && item->text(0).contains("Pre-release", Qt::CaseInsensitive)) {
                tree->setCurrentItem(item);
                tree->scrollToItem(item);
                tree->setFocus();
                emit tree->itemClicked(item, 0);
                break;
            }
        }
        return;
    }

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.github.com/repos/shadps4-emu/shadPS4/releases"));
    QNetworkReply* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, localHash, showMessage]() {
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Error"), reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray resp = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(resp);

        if (!doc.isArray()) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("The GitHub API response is not a valid JSON array."));
            reply->deleteLater();
            return;
        }

        QJsonArray arr = doc.array();
        QString latestHash;
        QString latestTag;

        for (const QJsonValue& val : arr) {
            QJsonObject obj = val.toObject();
            if (obj["prerelease"].toBool()) {

                latestTag = obj["tag_name"].toString();

                int idx = latestTag.lastIndexOf('-');
                if (idx != -1 && idx + 1 < latestTag.length()) {
                    latestHash = latestTag.mid(idx + 1);
                }

                break;
            }
        }

        if (latestHash.isEmpty()) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Unable to get hash of latest pre-release."));
            reply->deleteLater();
            return;
        }

        if (latestHash == localHash) {
            if (showMessage) {
                QMessageBox::information(this, tr("Auto Updater - Emulator"),
                                         tr("You already have the latest pre-release version."));
            }
        } else {
            showPreReleaseUpdateDialog(localHash, latestHash, latestTag);
        }

        reply->deleteLater();
    });
}

void VersionDialog::showPreReleaseUpdateDialog(const QString& localHash, const QString& latestHash,
                                               const QString& latestTag) {
    if (localHash == "") {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Auto Updater - Emulator"));

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* imageLabel = new QLabel(&dialog);
    QPixmap pixmap(":/images/shadLauncher4.png");
    imageLabel->setPixmap(pixmap);
    imageLabel->setScaledContents(true);
    imageLabel->setFixedSize(50, 50);

    QLabel* titleLabel = new QLabel("<h2>" + tr("Update Available (Emulator)") + "</h2>", &dialog);

    headerLayout->addWidget(imageLabel);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    mainLayout->addLayout(headerLayout);

    QString labelText = QString("<table>"
                                "<tr><td><b>%1:</b></td><td>%2</td></tr>"
                                "<tr><td><b>%3:</b></td><td>%4</td></tr>"
                                "</table>")
                            .arg(tr("Current Version"), localHash.left(7), tr("Latest Version"),
                                 latestHash.left(7));
    QLabel* infoLabel = new QLabel(labelText, &dialog);
    mainLayout->addWidget(infoLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QLabel* questionLabel = new QLabel(tr("Do you want to update?"), &dialog);
    QPushButton* btnUpdate = new QPushButton(tr("Update"), &dialog);
    QPushButton* btnCancel = new QPushButton(tr("No"), &dialog);

    btnLayout->addWidget(questionLabel);
    btnLayout->addStretch(1);
    btnLayout->addWidget(btnUpdate);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    // Changelog
    QTextBrowser* changelogView = new QTextBrowser(&dialog);
    changelogView->setReadOnly(true);
    changelogView->setVisible(false);
    changelogView->setFixedWidth(500);
    changelogView->setFixedHeight(200);
    mainLayout->addWidget(changelogView);

    QPushButton* toggleButton = new QPushButton(tr("Show Changelog"), &dialog);
    mainLayout->addWidget(toggleButton);

    connect(btnCancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    connect(btnUpdate, &QPushButton::clicked, this, [this, &dialog, latestTag]() {
        installPreReleaseByTag(latestTag);
        dialog.accept();
    });

    connect(toggleButton, &QPushButton::clicked, this,
            [this, changelogView, toggleButton, &dialog, localHash, latestHash, latestTag]() {
                if (!changelogView->isVisible()) {
                    requestChangelog(localHash, latestHash, latestTag, changelogView);
                    changelogView->setVisible(true);
                    toggleButton->setText(tr("Hide Changelog"));
                    dialog.adjustSize();
                } else {
                    changelogView->setVisible(false);
                    toggleButton->setText(tr("Show Changelog"));
                    dialog.adjustSize();
                }
            });
    if (m_gui_settings->GetValue(GUI::version_manager_showChangeLog).toBool()) {
        requestChangelog(localHash, latestHash, latestTag, changelogView);
        changelogView->setVisible(true);
        toggleButton->setText(tr("Hide Changelog"));
        dialog.adjustSize();
    }

    dialog.exec();
}

void VersionDialog::requestChangelog(const QString& localHash, const QString& latestHash,
                                     const QString& latestTag, QTextBrowser* outputView) {
    QString compareUrlString =
        QString("https://api.github.com/repos/shadps4-emu/shadPS4/compare/%1...%2")
            .arg(localHash, latestHash);

    QUrl compareUrl(compareUrlString);
    QNetworkRequest req(compareUrl);
    QNetworkReply* reply = networkManager->get(req);

    connect(
        reply, &QNetworkReply::finished, this, [this, reply, localHash, latestHash, outputView]() {
            if (reply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(this, tr("Error"),
                                     tr("Network error while fetching changelog") + ":\n" +
                                         reply->errorString());
                reply->deleteLater();
                return;
            }
            QByteArray resp = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(resp);
            QJsonObject obj = doc.object();
            QJsonArray commits = obj["commits"].toArray();

            QString changesHtml;
            for (const QJsonValue& cval : commits) {
                QJsonObject cobj = cval.toObject();
                QString msg = cobj["commit"].toObject()["message"].toString();
                int newlinePos = msg.indexOf('\n');
                if (newlinePos != -1) {
                    msg = msg.left(newlinePos);
                }
                if (!changesHtml.isEmpty()) {
                    changesHtml += "<br>";
                }
                changesHtml += "&nbsp;&nbsp;&nbsp;&nbsp;• " + msg;
            }

            // PR number as link ( #123 )
            QRegularExpression re("\\(\\#(\\d+)\\)");
            QString newText;
            int last = 0;
            auto it = re.globalMatch(changesHtml);
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                newText += changesHtml.mid(last, m.capturedStart() - last);
                QString num = m.captured(1);
                newText +=
                    QString("(<a href=\"https://github.com/shadps4-emu/shadPS4/pull/%1\">#%1</a>)")
                        .arg(num);
                last = m.capturedEnd();
            }
            newText += changesHtml.mid(last);
            changesHtml = newText;

            outputView->setOpenExternalLinks(true);
            outputView->setHtml("<h3>" + tr("Changes") + ":</h3>" + changesHtml);
            reply->deleteLater();
        });
}

void VersionDialog::installPreReleaseByTag(const QString& tagName) {
    QString apiUrl =
        QString("https://api.github.com/repos/shadps4-emu/shadPS4/releases/tags/%1").arg(tagName);

    QNetworkAccessManager* mgr = new QNetworkAccessManager(this);
    QNetworkRequest req(apiUrl);
    QNetworkReply* reply = mgr->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, tagName]() {
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Error"), reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray bytes = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(bytes);
        QJsonObject obj = doc.object();
        QJsonArray assets = obj["assets"].toArray();

        QString downloadUrl;
        QString platformStr;
#ifdef Q_OS_WIN
        platformStr = "win64-sdl";
#elif defined(Q_OS_LINUX)
        platformStr = "linux-sdl";
#elif defined(Q_OS_MACOS)
        platformStr = "macos-sdl";
#endif
        for (const QJsonValue& av : assets) {
            QJsonObject aobj = av.toObject();
            if (aobj["name"].toString().contains(platformStr)) {
                downloadUrl = aobj["browser_download_url"].toString();
                break;
            }
        }
        if (downloadUrl.isEmpty()) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("No download URL found for the specified asset."));
            reply->deleteLater();
            return;
        }
        showDownloadDialog(tagName, downloadUrl);

        reply->deleteLater();
    });
}

void VersionDialog::showDownloadDialog(const QString& tagName, const QString& downloadUrl) {
    QString userPath = m_gui_settings->GetValue(GUI::version_manager_versionPath).toString();
    QString zipPath = QDir(userPath).filePath("temp_pre_release_download.zip");
    QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    QString appExePath = appDir + "/shadPS4.exe";
#elif defined(Q_OS_LINUX)
    QString appExePath = appDir + "/shadPS4";
#elif defined(Q_OS_MACOS)
    QString appExePath = appDir + "/shadPS4.app/Contents/MacOS/shadPS4";
#endif

    disconnect(m_downloader, nullptr, this, nullptr);

    m_downloader->DownloadJSONWithCache(downloadUrl.toStdString(), zipPath, true,
                                        tr("Downloading Pre-release (Nightly)"), 0);

    connect(m_downloader, &Downloader::SignalDownloadError, this, [this](const QString& err) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Network error while downloading") + ":\n" + err);
    });

    connect(
        m_downloader, &Downloader::SignalDownloadFinished, this,
        [this, userPath, zipPath, appExePath, tagName]() {
            QString destFolder = QDir(userPath).filePath("Pre-release");

            try {
                Zip::Extract(zipPath, destFolder);
                QFile::remove(zipPath);
            } catch (const std::exception& e) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Extraction failure:") + ("\n") + e.what());
                return;
            }

            if (!QDir(destFolder).exists()) {
                QMessageBox::critical(this, tr("Error"), tr("Extraction failure."));
                return;
            }

            // Find the executable in the extracted folder
            QString versionExePath = "";
            QDirIterator it(destFolder, QDirIterator::Subdirectories);

            while (it.hasNext()) {
                it.next();
                QFileInfo fileInfo = it.fileInfo();

#ifdef Q_OS_WIN
                if (fileInfo.isFile() &&
                    fileInfo.suffix().compare("exe", Qt::CaseInsensitive) == 0) {
                    versionExePath = fileInfo.absoluteFilePath();
                    break;
                }
#elif defined(Q_OS_LINUX)
                if (fileInfo.isFile() && fileInfo.isExecutable() &&
                    !fileInfo.fileName().contains('.')) {
                    versionExePath = fileInfo.absoluteFilePath();
                    break;
                }
#elif defined(Q_OS_MACOS)
                if (fileInfo.isFile() && fileInfo.isExecutable() &&
                    fileInfo.fileName() == "shadPS4") {
                    versionExePath = fileInfo.absoluteFilePath();
                    break;
                }
#endif
            }

            if (versionExePath.isEmpty()) {
                // Fallback to using GetVersionExecutablePath
                versionExePath = m_gui_settings->GetVersionExecutablePath("Pre-release");
            }

            if (versionExePath.isEmpty() || !QFile::exists(versionExePath)) {
                QMessageBox::warning(this, tr("Error"),
                                     tr("Could not find executable in extracted files."));
                return;
            }

            if (QFile::exists(appExePath)) {
                // Create backup of old executable
                QString backupPath = appExePath + ".backup";
                if (QFile::exists(backupPath)) {
                    QFile::remove(backupPath);
                }
                QFile::rename(appExePath, backupPath);
            }

            // Copy to application directory
            bool copySuccess = QFile::copy(versionExePath, appExePath);

#ifdef Q_OS_LINUX
            // Set executable permissions
            if (copySuccess) {
                QFile(appExePath)
                    .setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                    QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther |
                                    QFile::ExeOther);
            }
#endif

            if (!copySuccess) {
                QMessageBox::warning(this, tr("Error"),
                                     // clang-format off
tr("Failed to copy executable to application directory.\nThe pre-release version has been saved to: %1").arg(destFolder));
                    // clang-format off
                } else {
                    QMessageBox::information(
                        this, tr("Success"),
                        tr("Pre-release (Nightly) has been:") + ("\n\n ") +
                            tr("1. Downloaded to:") + QString(" %1\n\n ").arg(destFolder) +
                            tr("2. Installed to:") + QString(" %1").arg(appExePath));
                }

                QRegularExpression re("-([a-fA-F0-9]{7,})$");
                QRegularExpressionMatch match = re.match(tagName);
                QString codename = match.hasMatch() ? match.captured(1) : "unknown";

                VersionManager::Version new_version{
                    .name = "Pre-release (Nightly)",
                    .path = versionExePath.toStdString(), // Keep version folder path
                    .date = QLocale::system()
                                .toString(QDate::currentDate(), QLocale::ShortFormat)
                                .toStdString(),
                    .codename = codename.toStdString(),
                    .type = VersionManager::VersionType::Nightly,
                };

                // Update or add the pre-release version
                VersionManager::UpdatePrerelease(new_version);

                // Also save the app directory path
                m_gui_settings->SetValue(GUI::version_manager_versionSelected, versionExePath);

                LoadInstalledList();
            });
}