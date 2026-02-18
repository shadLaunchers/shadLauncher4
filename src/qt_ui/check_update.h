// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef CHECKUPDATE_H
#define CHECKUPDATE_H

#include <memory>
#include <QCheckBox>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QPushButton>

#include "core/emulator_settings.h"
#include "gui_settings.h"

class CheckUpdate : public QDialog {
    Q_OBJECT

public:
    explicit CheckUpdate(std::shared_ptr<GUISettings> gui_settings, bool showMessage,
                         QWidget* parent = nullptr);

    ~CheckUpdate();

private slots:
    void CheckForUpdates(bool showMessage);
    void DownloadUpdate(const QString& url);
    void Install();

private:
    void setupUI(const QString& downloadUrl, const QString& latestDate, const QString& latestRev,
                 const QString& currentDate, const QString& currentRev);

    void requestChangelog(const QString& currentRev, const QString& latestRev,
                          const QString& downloadUrl, const QString& latestDate,
                          const QString& currentDate);

    QCheckBox* autoUpdateCheckBox{nullptr};
    QPushButton* yesButton{nullptr};
    QPushButton* noButton{nullptr};

    QNetworkAccessManager* networkManager{nullptr};

    std::shared_ptr<GUISettings> m_gui_settings;
};

#endif // CHECKUPDATE_H
