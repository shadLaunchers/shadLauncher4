// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <common/path_util.h>
// #include <common/scm_rev.h>
#include <common/zip_util.h>

#include "check_update.h"
#include "gui_settings.h"

using namespace Common::FS;

CheckUpdate::CheckUpdate(std::shared_ptr<GUISettings> gui_settings, bool showMessage,
                         QWidget* parent)
    : QDialog(parent), m_gui_settings(std::move(gui_settings)),
      networkManager(new QNetworkAccessManager(this)) {

    setWindowTitle(tr("Auto Updater - GUI"));
    setFixedSize(0, 0);
    CheckForUpdates(showMessage);
}

CheckUpdate::~CheckUpdate() {}

void CheckUpdate::CheckForUpdates(const bool showMessage) {
    QUrl url;

    bool checkName = true;
    while (checkName) {
        url = QUrl("https://api.github.com/repos/shadLaunchers/shadLauncher4/releases");
        checkName = false;
    }

    QNetworkRequest request(url);
    QNetworkReply* reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, showMessage]() {
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
                QString response = reply->readAll();
                if (response.startsWith("{\"message\":\"API rate limit exceeded for")) {
                    QMessageBox::warning(
                        this, tr("Auto Updater"),
                        // clang-format off
tr("The Auto Updater allows up to 60 update checks per hour.\\nYou have reached this limit. Please try again later.").replace("\\n", "\n"));
                    // clang-format on
                } else {
                    QMessageBox::warning(
                        this, tr("Error"),
                        QString(tr("Network error:") + "\n" + reply->errorString()));
                }
            } else {
                QMessageBox::warning(this, tr("Error"),
                                     QString(tr("Network error:") + "\n" + reply->errorString()));
            }
            reply->deleteLater();
            return;
        }

        QByteArray response = reply->readAll();
        QJsonDocument jsonDoc(QJsonDocument::fromJson(response));

        if (jsonDoc.isNull()) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to parse update information."));
            reply->deleteLater();
            return;
        }

        QString downloadUrl;
        QString latestVersion;
        QString latestRev;
        QString latestDate;
        QString platformString;

#ifdef Q_OS_WIN
        platformString = "win64-qt";
#elif defined(Q_OS_LINUX)
        platformString = "linux-qt";
#elif defined(Q_OS_MAC)
        platformString = "macos-qt";
#endif

        QJsonObject jsonObj;
        QJsonArray jsonArray = jsonDoc.array();
        for (const QJsonValue& value : jsonArray) {
            jsonObj = value.toObject();
            if (jsonObj.contains("prerelease") && jsonObj["prerelease"].toBool()) {
                break;
            }
        }
        if (!jsonObj.isEmpty()) {
            latestVersion = jsonObj["tag_name"].toString();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("No pre-releases found."));
            reply->deleteLater();
            return;
        }

        latestRev = latestVersion.right(40);
        latestDate = jsonObj["published_at"].toString();

        QJsonArray assets = jsonObj["assets"].toArray();
        bool found = false;

        for (const QJsonValue& assetValue : assets) {
            QJsonObject assetObj = assetValue.toObject();
            if (assetObj["name"].toString().contains(platformString)) {
                downloadUrl = assetObj["browser_download_url"].toString();
                found = true;
                break;
            }
        }

        if (!found) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("No download URL found for the specified asset."));
            reply->deleteLater();
            return;
        }

        // QString currentRev = QString::fromStdString(Common::g_scm_rev);
        // QString currentDate = Common::g_scm_date;
        // test
        QString currentRev = "55e70dedd9c2ac168e5a7c9cd62c80f28aa126f9";
        QString currentDate = "2026-02-10";

        QDateTime dateTime = QDateTime::fromString(latestDate, Qt::ISODate);
        latestDate = dateTime.isValid() ? dateTime.toString("yyyy-MM-dd HH:mm:ss") : "Unknown date";

        if (latestRev == currentRev) {
            if (showMessage) {
                QMessageBox::information(this, tr("Auto Updater"),
                                         tr("Your version is already up to date!"));
            }
            close();
            return;
        } else {
            setupUI(downloadUrl, latestDate, latestRev, currentDate, currentRev);
        }
        reply->deleteLater();
    });
}

void CheckUpdate::setupUI(const QString& downloadUrl, const QString& latestDate,
                          const QString& latestRev, const QString& currentDate,
                          const QString& currentRev) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    QHBoxLayout* titleLayout = new QHBoxLayout();

    QLabel* imageLabel = new QLabel(this);
    QPixmap pixmap(":/images/shadLauncher4.png");
    imageLabel->setPixmap(pixmap);
    imageLabel->setScaledContents(true);
    imageLabel->setFixedSize(50, 50);

    QLabel* titleLabel = new QLabel("<h1>" + tr("Update Available - GUI") + "</h1>", this);
    titleLayout->addWidget(imageLabel);
    titleLayout->addWidget(titleLabel);
    layout->addLayout(titleLayout);

    QString updateText = QString("<p>"
                                 "<table><tr>"
                                 "<td><b>" +
                                 tr("Current Version") +
                                 ":</b></td>"
                                 "<td>%1</td>"
                                 "<td>(%2)</td>"
                                 "</tr><tr>"
                                 "<td><b>" +
                                 tr("Latest Version") +
                                 ":</b></td>"
                                 "<td>%3</td>"
                                 "<td>(%4)</td>"
                                 "</tr></table></p>")
                             .arg(currentRev.left(7), currentDate, latestRev.left(7), latestDate);

    QLabel* updateLabel = new QLabel(updateText, this);
    layout->addWidget(updateLabel);

    // Setup bottom layout with action buttons
    autoUpdateCheckBox = new QCheckBox(tr("Check for Updates at Startup"), this);
    layout->addWidget(autoUpdateCheckBox);

    QHBoxLayout* updatePromptLayout = new QHBoxLayout();
    QLabel* updatePromptLabel = new QLabel(tr("Do you want to update?"), this);
    updatePromptLayout->addWidget(updatePromptLabel);

    yesButton = new QPushButton(tr("Update"), this);
    noButton = new QPushButton(tr("No"), this);
    yesButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    noButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    QSpacerItem* spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    updatePromptLayout->addItem(spacer);
    updatePromptLayout->addWidget(yesButton);
    updatePromptLayout->addWidget(noButton);

    layout->addLayout(updatePromptLayout);

    // Don't show changelog button if:
    // The current version is a pre-release and the version to be downloaded is a release.
    bool current_isWIP = currentRev.endsWith("WIP", Qt::CaseInsensitive);
    bool latest_isWIP = latestRev.endsWith("WIP", Qt::CaseInsensitive);
    if (current_isWIP && !latest_isWIP) {
    } else {
        QTextBrowser* textField = new QTextBrowser(this);
        textField->setReadOnly(true);
        textField->setFixedWidth(500);
        textField->setFixedHeight(200);
        textField->setVisible(false);
        layout->addWidget(textField);

        QPushButton* toggleButton = new QPushButton(tr("Show Changelog"), this);
        layout->addWidget(toggleButton);

        connect(toggleButton, &QPushButton::clicked,
                [this, textField, toggleButton, currentRev, latestRev, downloadUrl, latestDate,
                 currentDate]() {
                    if (!textField->isVisible()) {
                        requestChangelog(currentRev, latestRev, downloadUrl, latestDate,
                                         currentDate);
                        textField->setVisible(true);
                        toggleButton->setText(tr("Hide Changelog"));
                        adjustSize();
                        textField->setFixedWidth(textField->width() + 20);
                    } else {
                        textField->setVisible(false);
                        toggleButton->setText(tr("Show Changelog"));
                        adjustSize();
                    }
                });

        if (m_gui_settings->GetValue(GUI::general_show_changelog).toBool()) {
            requestChangelog(currentRev, latestRev, downloadUrl, latestDate, currentDate);
            textField->setVisible(true);
            toggleButton->setText(tr("Hide Changelog"));
            adjustSize();
            textField->setFixedWidth(textField->width() + 20);
        }
    }

    connect(yesButton, &QPushButton::clicked, this, [this, downloadUrl]() {
        yesButton->setEnabled(false);
        noButton->setEnabled(false);
        DownloadUpdate(downloadUrl);
    });

    connect(noButton, &QPushButton::clicked, this, [this]() { close(); });

    autoUpdateCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::general_check_gui_updates).toBool());

    connect(autoUpdateCheckBox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        m_gui_settings->SetValue(GUI::general_check_gui_updates, state == Qt::Checked);
    });

    setLayout(layout);
}

void CheckUpdate::requestChangelog(const QString& currentRev, const QString& latestRev,
                                   const QString& downloadUrl, const QString& latestDate,
                                   const QString& currentDate) {
    QString compareUrlString =
        QString("https://api.github.com/repos/shadLaunchers/shadLauncher4/compare/%1...%2")
            .arg(currentRev)
            .arg(latestRev);

    QUrl compareUrl(compareUrlString);
    QNetworkRequest compareRequest(compareUrl);
    QNetworkReply* compareReply = networkManager->get(compareRequest);

    connect(compareReply, &QNetworkReply::finished, this,
            [this, compareReply, downloadUrl, latestDate, latestRev, currentDate, currentRev]() {
                if (compareReply->error() != QNetworkReply::NoError) {
                    QMessageBox::warning(
                        this, tr("Error"),
                        QString(tr("Network error:") + "\n%1").arg(compareReply->errorString()));
                    compareReply->deleteLater();
                    return;
                }

                QByteArray compareResponse = compareReply->readAll();
                QJsonDocument compareJsonDoc(QJsonDocument::fromJson(compareResponse));
                QJsonObject compareJsonObj = compareJsonDoc.object();
                QJsonArray commits = compareJsonObj["commits"].toArray();

                QString changes;
                for (const QJsonValue& commitValue : commits) {
                    QJsonObject commitObj = commitValue.toObject();
                    QString message = commitObj["commit"].toObject()["message"].toString();

                    // Remove texts after first line break, if any, to make it cleaner
                    int newlineIndex = message.indexOf('\n');
                    if (newlineIndex != -1) {
                        message = message.left(newlineIndex);
                    }
                    if (!changes.isEmpty()) {
                        changes += "<br>";
                    }
                    changes += "&nbsp;&nbsp;&nbsp;&nbsp;• " + message;
                }

                // Update the text field with the changelog
                QTextBrowser* textField = findChild<QTextBrowser*>();
                if (textField) {
                    QRegularExpression re("\\(\\#(\\d+)\\)");
                    QString newChanges;
                    int lastIndex = 0;
                    QRegularExpressionMatchIterator i = re.globalMatch(changes);
                    while (i.hasNext()) {
                        QRegularExpressionMatch match = i.next();
                        newChanges += changes.mid(lastIndex, match.capturedStart() - lastIndex);
                        QString num = match.captured(1);
                        newChanges += QString("(<a "
                                              "href=\"https://github.com/shadLaunchers/"
                                              "shadLauncher4/pull/%1\">#%1</a>)")
                                          .arg(num);
                        lastIndex = match.capturedEnd();
                    }

                    newChanges += changes.mid(lastIndex);
                    changes = newChanges;

                    textField->setOpenExternalLinks(true);
                    textField->setHtml("<h2>" + tr("Changes") + ":</h2>" + changes);
                }

                compareReply->deleteLater();
            });
}

void CheckUpdate::DownloadUpdate(const QString& url) {
    QProgressBar* progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setTextVisible(true);
    progressBar->setValue(0);

    layout()->addWidget(progressBar);

    QNetworkRequest request(url);
    QNetworkReply* reply = networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [progressBar](qint64 bytesReceived, qint64 bytesTotal) {
                if (bytesTotal > 0) {
                    int percentage = static_cast<int>((bytesReceived * 100) / bytesTotal);
                    progressBar->setValue(percentage);
                }
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply, progressBar, url]() {
        progressBar->setValue(100);
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Network error occurred while trying to access the URL") +
                                     ":\n" + url + "\n" + reply->errorString());
            reply->deleteLater();
            progressBar->deleteLater();
            return;
        }

        QString userPath;
        Common::FS::PathToQString(userPath, Common::FS::GetUserPath(Common::FS::PathType::UserDir));
#ifdef Q_OS_WIN
        QString tempDownloadPath =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/Temp/temp_download_update_gui";
#else
        QString tempDownloadPath = userPath + "/temp_download_update_gui";
#endif
        QDir dir(tempDownloadPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        QString downloadPath = tempDownloadPath + "/temp_download_update.zip";
        QFile file(downloadPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            QMessageBox::information(this, tr("Download Complete"),
                                     tr("The update has been downloaded, press OK to install."));
            Install();
        } else {
            QMessageBox::warning(
                this, tr("Error"),
                QString(tr("Failed to save the update file at") + ":\n" + downloadPath));
        }

        reply->deleteLater();
        progressBar->deleteLater();
    });
}

void CheckUpdate::Install() {
    QString tempDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                          "/Temp/temp_download_update_gui";

    QString rootPath;
    Common::FS::PathToQString(rootPath, std::filesystem::current_path());

    // 1. Extract the ZIP file
    try {
        QString zipFilePath = tempDirPath + "/temp_download_update.zip";
        Zip::Extract(zipFilePath, tempDirPath);

        // Delete the ZIP file after extraction.
        QFile::remove(zipFilePath);

    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to extract the update ZIP:\n") + e.what());
        return;
    }

    // 2. Creates a PowerShell/Bash script to copy files and launch the launcher.
    QString scriptFileName = tempDirPath + "/update.ps1";
    QString scriptContent;

#ifdef Q_OS_WIN
    scriptContent = QString("Copy-Item -Path '%1\\*' -Destination '%2\\' -Recurse -Force\n"
                            "Start-Sleep -Milliseconds 500\n"
                            "Start-Process powershell -ArgumentList \"-Command Remove-Item -Force "
                            "-LiteralPath '%2\\\\update.ps1'\" -WindowStyle Hidden\n"
                            "Start-Process -FilePath '%2\\shadLauncher4.exe'\n"
                            "Remove-Item -Recurse -Force '%1'\n")
                        .arg(tempDirPath, rootPath);

    QFile scriptFile(scriptFileName);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to create update script:\n") + scriptFileName);
        return;
    }
    QTextStream out(&scriptFile);
    out << scriptContent;
    scriptFile.close();

    QStringList arguments;
    arguments << "-ExecutionPolicy" << "Bypass" << "-File" << scriptFileName;
    QString processCommand = "powershell.exe";

#elif defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    // Unix-like (Linux/macOS)
    scriptFileName = tempDirPath + "/update.sh";
    scriptContent = QString("#!/bin/bash\n"
                            "cp -r '%1/'* '%2/'\n"
                            "rm -r '%1'\n"
                            "chmod +x '%2/shadLauncher4-qt.AppImage'\n"
                            "(sh -c \"sleep 1; rm -f '%2/update.sh'\") &\n"
                            "cd '%2' && ./shadLauncher4-qt.AppImage\n")
                        .arg(tempDirPath, rootPath);

    QFile scriptFile(scriptFileName);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to create update script:\n") + scriptFileName);
        return;
    }
    QTextStream out(&scriptFile);
    out << scriptContent;
    scriptFile.close();

    scriptFile.setPermissions(QFileDevice::ExeOwner | QFileDevice::ReadOwner |
                              QFileDevice::WriteOwner);

    QStringList arguments;
    arguments << scriptFileName;
    QString processCommand = "bash";
#endif

    // 3. Starts the script and closes the current shadLauncher4
    if (!QProcess::startDetached(processCommand, arguments)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to start update process."));
        return;
    }

    exit(EXIT_SUCCESS);
}
