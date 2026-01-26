// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "progress_indicator.h"

#include <QProgressDialog>

class ProgressDialog : public QProgressDialog {
public:
    ProgressDialog(const QString& windowTitle, const QString& labelText,
                   const QString& cancelButtonText, int minimum, int maximum, bool delete_on_close,
                   QWidget* parent = Q_NULLPTR, Qt::WindowFlags flags = Qt::WindowFlags());
    ~ProgressDialog();
    void SetRange(int min, int max);
    void SetValue(int progress);
    void SetDeleteOnClose();
    void SignalFailure() const;

private:
    std::unique_ptr<ProgressIndicator> m_progress_indicator;
};
