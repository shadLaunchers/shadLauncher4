// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "progress_dialog.h"

#include <QApplication>
#include <QLabel>

ProgressDialog::ProgressDialog(const QString& windowTitle, const QString& labelText,
                               const QString& cancelButtonText, int minimum, int maximum,
                               bool delete_on_close, QWidget* parent, Qt::WindowFlags flags)
    : QProgressDialog(labelText, cancelButtonText, minimum, maximum, parent, flags) {
    setWindowTitle(windowTitle);
    setMinimumSize(QLabel("This is the very length of the progressdialog due to hidpi reasons.")
                       .sizeHint()
                       .width(),
                   sizeHint().height());
    setValue(0);
    setWindowModality(Qt::WindowModal);

    if (delete_on_close) {
        SetDeleteOnClose();
    }

    m_progress_indicator = std::make_unique<ProgressIndicator>(minimum, maximum);
}

ProgressDialog::~ProgressDialog() {}

void ProgressDialog::SetRange(int min, int max) {
    m_progress_indicator->SetRange(min, max);

    setRange(min, max);
}

void ProgressDialog::SetValue(int progress) {
    const int value = std::clamp(progress, minimum(), maximum());

    m_progress_indicator->SetValue(value);

    setValue(value);
}

void ProgressDialog::SetDeleteOnClose() {
    setAttribute(Qt::WA_DeleteOnClose);
    connect(this, &QProgressDialog::canceled, this, &QProgressDialog::close, Qt::UniqueConnection);
}

void ProgressDialog::SignalFailure() const {
    m_progress_indicator->SignalFailure();

    QApplication::beep();
}
