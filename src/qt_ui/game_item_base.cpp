// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "game_item_base.h"

GameItemBase::GameItemBase() {
    m_icon_loading_aborted.reset(new std::atomic<bool>(false));
    m_size_on_disk_loading_aborted.reset(new std::atomic<bool>(false));
}

GameItemBase::~GameItemBase() {
    waitForIconLoading(true);
    waitForSizeOnDiskLoading(true);
}

void GameItemBase::getIconLoadFunc(int index) {
    if (!m_icon_load_callback || m_icon_loading || m_icon_loading_aborted->load()) {
        return;
    }

    waitForIconLoading(true);

    *m_icon_loading_aborted = false;
    m_icon_loading = true;
    m_icon_load_thread.reset(QThread::create([this, index]() {
        if (m_icon_load_callback) {
            m_icon_load_callback(index);
        }
    }));
    m_icon_load_thread->start();
}

void GameItemBase::setIconLoadFunc(const icon_load_callback_t& func) {
    waitForIconLoading(true);

    m_icon_loading = false;
    m_icon_load_callback = func;
    *m_icon_loading_aborted = false;
}

void GameItemBase::getSizeCalcFunc() {
    if (!m_size_calc_callback || m_size_on_disk_loading || m_size_on_disk_loading_aborted->load()) {
        return;
    }

    waitForSizeOnDiskLoading(true);

    *m_size_on_disk_loading_aborted = false;
    m_size_on_disk_loading = true;
    m_size_calc_thread.reset(QThread::create([this]() {
        if (m_size_calc_callback) {
            m_size_calc_callback();
        }
    }));
    m_size_calc_thread->start();
}

void GameItemBase::setSizeCalcFunc(const size_calc_callback_t& func) {
    m_size_on_disk_loading = false;
    m_size_calc_callback = func;
    *m_size_on_disk_loading_aborted = false;
}

void GameItemBase::waitForIconLoading(bool abort) {
    *m_icon_loading_aborted = abort;

    if (m_icon_load_thread && m_icon_load_thread->isRunning()) {
        m_icon_load_thread->wait();
        m_icon_load_thread.reset();
    }
}

void GameItemBase::waitForSizeOnDiskLoading(bool abort) {
    *m_size_on_disk_loading_aborted = abort;

    if (m_size_calc_thread && m_size_calc_thread->isRunning()) {
        m_size_calc_thread->wait();
        m_size_calc_thread.reset();
    }
}
