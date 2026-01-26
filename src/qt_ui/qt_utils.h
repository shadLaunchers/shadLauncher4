// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QStyleHints>
#include <QTableWidget>
#include <QTreeWidgetItem>

#include <filesystem>
#include <map>
#include <string>

namespace GUI {
namespace Utils {

class CirclePixmap : public QPixmap {
public:
    CirclePixmap(const QColor& color, qreal pixel_ratio)
        : QPixmap(pixel_ratio * 16, pixel_ratio * 16) {
        fill(Qt::transparent);

        QPainter painter(this);
        setDevicePixelRatio(pixel_ratio);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(0, 0, width(), height());
        painter.end();
    }
};

template <typename T>
static QSet<T> ListToSet(const QList<T>& list) {
    return QSet<T>(list.begin(), list.end());
}

// Convert an arbitrary count of bytes to a readable format using global units (KB, MB...)
QString FormatByteSize(u64 size);

// Returns the color specified by its color_role for the QLabels with object_name
QColor GetLabelColor(const QString& object_name, const QColor& fallback_light,
                     const QColor& fallback_dark,
                     QPalette::ColorRole color_role = QPalette::WindowText);

template <typename T>
void StopFutureWatcher(QFutureWatcher<T>& watcher, bool cancel,
                       std::shared_ptr<std::atomic<bool>> cancel_flag = nullptr) {
    if (watcher.isSuspended() || watcher.isRunning()) {
        watcher.resume();

        if (cancel) {
            watcher.cancel();

            // We use an optional cancel flag since the QFutureWatcher::canceled signal seems to be
            // very unreliable
            if (cancel_flag) {
                *cancel_flag = true;
            }
        }
        watcher.waitForFinished();
    }
}
static inline Qt::ColorScheme ColorScheme() {
    // use the QGuiApplication's properties to report the default GUI color scheme
    return QGuiApplication::styleHints()->colorScheme();
}

static inline bool DarkModeActive() {
    // "true" if the default GUI color scheme is dark. "false" otherwise
    return ColorScheme() == Qt::ColorScheme::Dark;
}

static inline std::string NormalizePath(const std::filesystem::path& p) {
    // Convert to canonical lexical form (purely lexical)
    auto np = p.lexically_normal();

    // Convert to UTF-8 string
    auto u8 = np.generic_u8string();
    std::string s(u8.begin(), u8.end());

#ifdef _WIN32
    // Normalize drive letter
    if (s.size() >= 2 && s[1] == ':')
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
#endif

    return s;
}

} // namespace Utils
} // namespace GUI