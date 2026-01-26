// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings.h"

namespace GUI {
namespace Persistent {

const QString persistent_file_name = "persistent_settings";

// Entry names
const QString playtime = "Playtime";
const QString last_played = "LastPlayed";
const QString notes = "Notes";
const QString titles = "Titles";

// Date format
const QString last_played_date_format_old = "MMMM d yyyy";
const QString last_played_date_format_new = "MMMM d, yyyy";
const QString last_played_date_with_time_of_day_format = "MMMM d, yyyy HH:mm";
const Qt::DateFormat last_played_date_format = Qt::DateFormat::ISODate;

} // namespace Persistent
} // namespace GUI
class PersistentSettings : public Settings {
    Q_OBJECT

public:
    explicit PersistentSettings(QObject* parent = nullptr);

public Q_SLOTS:
    void SetPlaytime(const QString& serial, quint64 playtime, bool sync);
    void AddPlaytime(const QString& serial, quint64 elapsed, bool sync);
    quint64 GetPlaytime(const QString& serial);

    void SetLastPlayed(const QString& serial, const QString& date, bool sync);
    QString GetLastPlayed(const QString& serial);

private:
    std::map<QString, quint64> m_playtime;
    std::map<QString, QString> m_last_played;
};