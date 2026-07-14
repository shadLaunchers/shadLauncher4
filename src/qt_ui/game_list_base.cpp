// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "game_list_base.h"

#include <QBuffer>
#include <QDir>
#include <QPainter>

#include <cmath>
#include <filesystem>
#include <functional>
#include <system_error>
#include <unordered_set>

#include "common/types.h"
#include "game_info_cache.h"

GameListBase::GameListBase() {}

namespace {
s64 ComputeIconFingerprint(const std::string& icon_path) {
    if (icon_path.empty()) {
        return 0;
    }

    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(icon_path, ec);
    if (ec) {
        return 0;
    }

    return static_cast<s64>(std::hash<std::string>{}(icon_path)) ^
           static_cast<s64>(ftime.time_since_epoch().count());
}
} // namespace

void GameListBase::RepaintIcons(std::vector<game_info>& game_data, const QColor& icon_color,
                                const QSize& icon_size, qreal device_pixel_ratio) {
    m_icon_size = icon_size;
    m_icon_color = icon_color;

    QPixmap placeholder(icon_size * device_pixel_ratio);
    placeholder.setDevicePixelRatio(device_pixel_ratio);
    placeholder.fill(Qt::transparent);

    for (game_info& game : game_data) {
        game->pxmap = placeholder;

        if (GameItemBase* item = game->item) {
            item->setIconLoadFunc(
                [this, game, device_pixel_ratio, cancel = item->getIconLoadingAborted()](int) {
                    IconLoadFunction(game, device_pixel_ratio, cancel);
                });

            item->getImageChangeCallback();
        }
    }
}

void GameListBase::IconLoadFunction(game_info game, qreal device_pixel_ratio,
                                    std::shared_ptr<std::atomic<bool>> cancel) {
    if (cancel && cancel->load()) {
        return;
    }

    if (game->icon.isNull() && !game->info.icon_path.empty()) {
        const s64 icon_fingerprint = ComputeIconFingerprint(game->info.icon_path);
        bool loaded = false;

        if (m_info_cache && icon_fingerprint != 0) {
            if (const auto cached = m_info_cache->GetIcon(game->info.path, icon_fingerprint)) {
                loaded = game->icon.loadFromData(*cached, "PNG");
            }
        }

        if (!loaded) {
            loaded = game->icon.load(QString::fromStdString(game->info.icon_path));

            if (loaded && m_info_cache && icon_fingerprint != 0) {
                QByteArray icon_bytes;
                QBuffer buffer(&icon_bytes);
                buffer.open(QIODevice::WriteOnly);
                if (game->icon.save(&buffer, "PNG")) {
                    m_info_cache->PutIcon(game->info.path, icon_bytes, icon_fingerprint);
                }
            }
        }
        // TODO log if fails?
    }

    if (!game->item || (cancel && cancel->load())) {
        return;
    }

    const QColor color = GetGridCompatibilityColor(game->compat.color);
    {
        std::lock_guard lock(game->item->pixmap_mutex);
        game->pxmap = PaintedPixmap(game->icon, device_pixel_ratio, game->has_custom_config,
                                    game->has_custom_pad_config, color);
    }

    if (!cancel || !cancel->load()) {
        if (m_icon_ready_callback)
            m_icon_ready_callback(game, game->item);
    }
}

QPixmap GameListBase::PaintedPixmap(const QPixmap& icon, qreal device_pixel_ratio,
                                    bool paint_config_icon, bool paint_pad_config_icon,
                                    const QColor& compatibility_color) const {
    QSize canvas_size(320, 176);
    QSize icon_size(icon.size());
    QPoint target_pos;

    if (!icon.isNull()) {
        // Let's upscale the original icon to at least fit into the outer rect of the size ICON0.PNG
        if (icon_size.width() < 320 || icon_size.height() < 176) {
            icon_size.scale(320, 176, Qt::KeepAspectRatio);
        }

        canvas_size = icon_size;

        // Calculate the centered size and position of the icon on our canvas.
        if (icon_size.width() != 320 || icon_size.height() != 176) {
            constexpr double target_ratio = 320.0 / 176.0; // aspect ratio 20:11

            if ((icon_size.width() / static_cast<double>(icon_size.height())) > target_ratio) {
                canvas_size.setHeight(std::ceil(icon_size.width() / target_ratio));
            } else {
                canvas_size.setWidth(std::ceil(icon_size.height() * target_ratio));
            }

            target_pos.setX(std::max<int>(0, (canvas_size.width() - icon_size.width()) / 2.0));
            target_pos.setY(std::max<int>(0, (canvas_size.height() - icon_size.height()) / 2.0));
        }
    }

    // Create a canvas large enough to fit our entire scaled icon
    QPixmap canvas(canvas_size * device_pixel_ratio);
    canvas.setDevicePixelRatio(device_pixel_ratio);
    canvas.fill(m_icon_color);

    // Create a painter for our canvas
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Draw the icon onto our canvas
    if (!icon.isNull()) {
        painter.drawPixmap(target_pos.x(), target_pos.y(), icon_size.width(), icon_size.height(),
                           icon);
    }

    // Draw config icons if necessary
    if (!m_is_list_layout && (paint_config_icon || paint_pad_config_icon)) {
        const int width = canvas_size.width() * 0.2;
        const QPoint origin = QPoint(canvas_size.width() - width, 0);
        QString icon_path;

        if (paint_config_icon && paint_pad_config_icon) {
            icon_path = ":/images/controllers_config_combo.png";
        } else if (paint_config_icon) {
            icon_path = ":/images/custom_config.png";
        } else if (paint_pad_config_icon) {
            icon_path = ":/images/controllers.png";
        }

        QPixmap custom_config_icon(icon_path);
        custom_config_icon.setDevicePixelRatio(device_pixel_ratio);
        painter.drawPixmap(origin,
                           custom_config_icon.scaled(QSize(width, width) * device_pixel_ratio,
                                                     Qt::KeepAspectRatio,
                                                     Qt::TransformationMode::SmoothTransformation));
    }

    // Draw game compatibility icons if necessary
    if (compatibility_color.isValid()) {
        const int size = canvas_size.height() * 0.2;
        const int spacing = canvas_size.height() * 0.05;
        QColor copyColor = QColor(compatibility_color);
        copyColor.setAlpha(215); // ~85% opacity
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QBrush(copyColor));
        painter.setPen(
            QPen(Qt::black, std::max(canvas_size.width() / 320, canvas_size.height() / 176)));
        painter.drawEllipse(spacing, spacing, size, size);
    }

    // Finish the painting
    painter.end();

    // Scale and return our final image
    return canvas.scaled(m_icon_size * device_pixel_ratio, Qt::KeepAspectRatio,
                         Qt::TransformationMode::SmoothTransformation);
}

QColor GameListBase::GetGridCompatibilityColor(const QString& string) const {
    if (m_draw_compat_status_to_grid && !m_is_list_layout) {
        return QColor(string);
    }
    return QColor();
}

QIcon GameListBase::GetCustomConfigIcon(const game_info& game) {
    if (!game)
        return {};

    static const QIcon icon_combo_config_bordered(":/images/controllers_config_combo.png");
    static const QIcon icon_custom_config(":/images/custom_config.png");
    static const QIcon icon_controllers(":/images/controllers.png");

    if (game->has_custom_config && game->has_custom_pad_config) {
        return icon_combo_config_bordered;
    }

    if (game->has_custom_config) {
        return icon_custom_config;
    }

    if (game->has_custom_pad_config) {
        return icon_controllers;
    }

    return {};
}
