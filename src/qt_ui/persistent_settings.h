// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings.h"

namespace GUI {
namespace Persistent {

const QString persistent_file_name = "persistent_settings";

// Entry names
const QString notes = "Notes";
const QString titles = "Titles";

const QString last_played_date_format_new = "dd/MM/yyyy";
const QString last_played_date_with_time_of_day_format = "dd/MM/yyyy HH:mm";

} // namespace Persistent
} // namespace GUI
class PersistentSettings : public Settings {
    Q_OBJECT

public:
    explicit PersistentSettings(QObject* parent = nullptr);
};