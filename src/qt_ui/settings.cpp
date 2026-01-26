// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/path_util.h>
#include "settings.h"

Settings::Settings(QObject* parent) : QObject(parent), m_settings_dir(ComputeSettingsDir()) {}

Settings::~Settings() {
    Sync();
}

void Settings::Sync() {
    if (m_settings) {
        m_settings->sync();
    }
}

QString Settings::GetSettingsDir() const {
    return m_settings_dir.absolutePath();
}

QString Settings::ComputeSettingsDir() {
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);

#ifdef _WIN32
    return QString::fromStdWString(config_dir.wstring()) + '/';
#else
    return QString::fromUtf8(config_dir.u8string().c_str()) + '/';
#endif
}

void Settings::RemoveValue(const QString& key, const QString& name, bool sync) const {
    if (m_settings) {
        m_settings->beginGroup(key);
        m_settings->remove(name);
        m_settings->endGroup();

        if (sync) {
            m_settings->sync();
        }
    }
}

void Settings::RemoveValue(const GUISave& entry, bool sync) const {
    RemoveValue(entry.key, entry.name, sync);
}

QVariant Settings::GetValue(const QString& key, const QString& name, const QVariant& def) const {
    return m_settings ? m_settings->value(key + "/" + name, def) : def;
}

QVariant Settings::GetValue(const GUISave& entry) const {
    return GetValue(entry.key, entry.name, entry.def);
}

QVariant Settings::List2Var(const QList<QPair<QString, QString>>& list) {
    QByteArray ba;
    QDataStream stream(&ba, QIODevice::WriteOnly);
    stream << list;
    return QVariant(ba);
}

QList<QPair<QString, QString>> Settings::Var2List(const QVariant& var) {
    QList<QPair<QString, QString>> list;
    QByteArray ba = var.toByteArray();
    QDataStream stream(&ba, QIODevice::ReadOnly);
    stream >> list;
    return list;
}

QList<QString> Settings::Var2StringList(const QVariant& var) {
    QList<QString> list;
    QByteArray ba = var.toByteArray();
    QDataStream stream(&ba, QIODevice::ReadOnly);
    stream >> list;
    return list;
}

QVariant Settings::StringList2Var(const QList<QString>& list) {
    QByteArray ba;
    QDataStream stream(&ba, QIODevice::WriteOnly);
    stream << list;
    return QVariant(ba);
}

QList<int> Settings::Var2IntList(const QVariant& var) {
    QList<int> intList;
    QList<QVariant> qVariantList = var.toList();
    for (const QVariant& item : qVariantList) {
        if (item.canConvert<int>()) {
            intList.append(item.toInt());
        }
    }

    return intList;
}

void Settings::SetValue(const GUISave& entry, const QVariant& value, bool sync) const {
    SetValue(entry.key, entry.name, value, sync);
}

void Settings::SetValue(const QString& key, const QVariant& value, bool sync) const {
    if (m_settings) {
        m_settings->setValue(key, value);

        if (sync) {
            m_settings->sync();
        }
    }
}

void Settings::SetValue(const QString& key, const QString& name, const QVariant& value,
                        bool sync) const {
    if (m_settings) {
        m_settings->beginGroup(key);
        m_settings->setValue(name, value);
        m_settings->endGroup();

        if (sync) {
            m_settings->sync();
        }
    }
}
