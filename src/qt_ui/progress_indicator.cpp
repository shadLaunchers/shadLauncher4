// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "progress_indicator.h"

#if HAVE_QTDBUS
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#endif

ProgressIndicator::ProgressIndicator(int minimum, int maximum) {
    m_minimum = minimum;
    m_maximum = maximum;
#if HAVE_QTDBUS
    UpdateProgress(0, true, false);
#endif
}

ProgressIndicator::~ProgressIndicator() {
#if HAVE_QTDBUS
    UpdateProgress(0, false, false);
#endif
}

int ProgressIndicator::GetValue() const {
    return m_value;
}

void ProgressIndicator::SetValue(int value) {
    m_value = std::clamp(value, m_minimum, m_maximum);
#if HAVE_QTDBUS
    UpdateProgress(m_value, true, false);
#endif
}

void ProgressIndicator::SetRange(int minimum, int maximum) {
    m_minimum = minimum;
    m_maximum = maximum;
}

void ProgressIndicator::Reset() {
    m_value = m_minimum;
#if HAVE_QTDBUS
    UpdateProgress(m_value, false, false);
#endif
}

void ProgressIndicator::SignalFailure() {
#if HAVE_QTDBUS
    UpdateProgress(0, false, true);
#endif
}

#if HAVE_QTDBUS
void ProgressIndicator::UpdateProgress(int progress, bool progress_visible, bool urgent) {
    QVariantMap properties;
    properties.insert(QStringLiteral("urgent"), urgent);

    if (!urgent) {
        // Progress takes a value from 0.0 to 0.1
        properties.insert(QStringLiteral("progress"), 1. * progress / m_maximum);
        properties.insert(QStringLiteral("progress-visible"), progress_visible);
    }

    QDBusMessage message = QDBusMessage::createSignal(
        QStringLiteral("/"), QStringLiteral("com.canonical.Unity.LauncherEntry"),
        QStringLiteral("Update"));

    message << QStringLiteral("application://rpcs3.desktop") << properties;

    QDBusConnection::sessionBus().send(message);
}
#endif
