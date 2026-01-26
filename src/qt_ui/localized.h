// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class Localized : public QObject {
    Q_OBJECT

public:
    Localized() {}

    QString getVerboseTimeByMs(quint64 elapsed_ms, bool show_days = false) const;
};
