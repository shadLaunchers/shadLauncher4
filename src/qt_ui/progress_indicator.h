// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class ProgressIndicator {
public:
    ProgressIndicator(int minimum, int maximum);
    ~ProgressIndicator();

    int GetValue() const;

    void SetValue(int value);
    void SetRange(int minimum, int maximum);
    void Reset();
    void SignalFailure();

private:
    int m_value = 0;
    int m_minimum = 0;
    int m_maximum = 100;
#if HAVE_QTDBUS
    void UpdateProgress(int progress, bool progress_visible, bool urgent);
#endif
};
