// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDir>
#include <QSettings>
#include <QSize>
#include <QVariant>

#include <memory>

#include "gui_save.h"

class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings();

    void Sync();

    QString GetSettingsDir() const;

    QVariant GetValue(const QString& key, const QString& name, const QVariant& def) const;
    QVariant GetValue(const GUISave& entry) const;
    static QVariant List2Var(const QList<QPair<QString, QString>>& list);
    static QList<QPair<QString, QString>> Var2List(const QVariant& var);
    static QVariant StringList2Var(const QList<QString>& list);
    static QList<QString> Var2StringList(const QVariant& var);
    static QList<int> Var2IntList(const QVariant& var);

public Q_SLOTS:
    /** Remove entry */
    void RemoveValue(const QString& key, const QString& name, bool sync = true) const;
    void RemoveValue(const GUISave& entry, bool sync = true) const;

    /** Write value to entry */
    void SetValue(const GUISave& entry, const QVariant& value, bool sync = true) const;
    void SetValue(const QString& key, const QVariant& value, bool sync = true) const;
    void SetValue(const QString& key, const QString& name, const QVariant& value,
                  bool sync = true) const;

protected:
    static QString ComputeSettingsDir();

    std::unique_ptr<QSettings> m_settings;
    QDir m_settings_dir;
};
