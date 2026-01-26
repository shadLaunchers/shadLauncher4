// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gui_settings.h"
#include "qt_utils.h"

namespace GUI {
namespace Utils {

QString FormatByteSize(u64 size) {
    u64 byte_unit = 0;
    u64 divisor = 1;
#if defined(__APPLE__)
    constexpr u64 multiplier = 1000;
    static const QString s_units[]{"B", "kB", "MB", "GB", "TB", "PB"};
#else
    constexpr u64 multiplier = 1024;
    static const QString s_units[]{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
#endif

    while (byte_unit < std::size(s_units) - 1 && size / divisor >= multiplier) {
        byte_unit++;
        divisor *= multiplier;
    }

    return QStringLiteral("%0 %1")
        .arg(QString::number((size + 0.) / divisor, 'f', 2))
        .arg(s_units[byte_unit]);
}
QColor GetLabelColor(const QString& object_name, const QColor& fallback_light,
                     const QColor& fallback_dark, QPalette::ColorRole color_role) {
    if (!GUI::custom_stylesheet_active || !GUI::stylesheet.contains(object_name)) {
        return DarkModeActive() ? fallback_dark : fallback_light;
    }

    QLabel dummy_color;
    dummy_color.setObjectName(object_name);
    dummy_color.ensurePolished();
    return dummy_color.palette().color(color_role);
    return fallback_light;
}

} // namespace Utils
} // namespace GUI