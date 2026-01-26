// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <shared_mutex>
#include <QThread>

using icon_load_callback_t = std::function<void(int)>;
using size_calc_callback_t = std::function<void()>;

class GameItemBase {
public:
    GameItemBase();
    virtual ~GameItemBase();

    void getIconLoadFunc(int index);
    void setIconLoadFunc(const icon_load_callback_t& func);

    void getSizeCalcFunc();
    void setSizeCalcFunc(const size_calc_callback_t& func);

    void waitForIconLoading(bool abort);
    void waitForSizeOnDiskLoading(bool abort);

    bool getIconLoading() const {
        return m_icon_loading;
    }

    bool getSizeOnDiskLoading() const {
        return m_size_on_disk_loading;
    }

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> getIconLoadingAborted() const {
        return m_icon_loading_aborted;
    }

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> getSizeOnDiskLoadingAborted() const {
        return m_size_on_disk_loading_aborted;
    }

    void getImageChangeCallback() const {
        if (m_image_change_callback) {
            m_image_change_callback();
        }
    }

    void setImageChangeCallback(const std::function<void()>& func) {
        m_image_change_callback = func;
    }

    std::shared_mutex pixmap_mutex;

private:
    std::unique_ptr<QThread> m_icon_load_thread;
    std::unique_ptr<QThread> m_size_calc_thread;
    std::atomic<bool> m_size_on_disk_loading{false};
    std::atomic<bool> m_icon_loading{false};
    size_calc_callback_t m_size_calc_callback = nullptr;
    icon_load_callback_t m_icon_load_callback = nullptr;

    std::shared_ptr<std::atomic<bool>> m_icon_loading_aborted;
    std::shared_ptr<std::atomic<bool>> m_size_on_disk_loading_aborted;
    std::function<void()> m_image_change_callback = nullptr;
};
