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
