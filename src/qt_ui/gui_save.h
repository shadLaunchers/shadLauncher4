// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QVariant>

struct GUISave {
    QString key;
    QString name;
    QVariant def;

    GUISave() {}

    GUISave(const QString& k, const QString& n, const QVariant& d) : key(k), name(n), def(d) {}

    bool operator==(const GUISave& rhs) const noexcept {
        return key == rhs.key && name == rhs.name && def == rhs.def;
    }
};
