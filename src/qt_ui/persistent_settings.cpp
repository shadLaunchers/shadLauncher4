// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "persistent_settings.h"

PersistentSettings::PersistentSettings(QObject* parent) : Settings(parent) {
    // Don't use the .ini file ending for now, as it will be confused for a regular gui_settings
    // file.
    m_settings = std::make_unique<QSettings>(ComputeSettingsDir() +
                                                 GUI::Persistent::persistent_file_name + ".dat",
                                             QSettings::Format::IniFormat, parent);
}

void PersistentSettings::SetPlaytime(const QString& serial, quint64 playtime, bool sync) {
    m_playtime[serial] = playtime;
    SetValue(GUI::Persistent::playtime, serial, playtime, sync);
}

void PersistentSettings::AddPlaytime(const QString& serial, quint64 elapsed, bool sync) {
    const quint64 playtime = GetValue(GUI::Persistent::playtime, serial, 0).toULongLong();
    SetPlaytime(serial, playtime + elapsed, sync);
}

quint64 PersistentSettings::GetPlaytime(const QString& serial) {
    return m_playtime[serial];
}

void PersistentSettings::SetLastPlayed(const QString& serial, const QString& date, bool sync) {
    m_last_played[serial] = date;
    SetValue(GUI::Persistent::last_played, serial, date, sync);
}

QString PersistentSettings::GetLastPlayed(const QString& serial) {
    return m_last_played[serial];
}
