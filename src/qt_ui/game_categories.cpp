// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "game_categories.h"
#include "game_info.h"
#include "game_info_cache.h"
#include "qt_utils.h"

GameCategories::GameCategories(std::shared_ptr<GameInfoCache> info_cache, QObject* parent)
    : QObject(parent), m_info_cache(std::move(info_cache)) {
    Load();
}

GameKey GameCategories::KeyFor(const GameInfo& info) {
    return GameKey{GUI::Utils::GameKeyOf(info)};
}

void GameCategories::Load() {
    m_order.clear();
    m_members.clear();

    if (!m_info_cache) {
        return;
    }

    for (const CategoryRecord& record : m_info_cache->LoadCategories()) {
        const QString name = record.name.trimmed();
        if (name.isEmpty() || Contains(name)) {
            continue;
        }

        QSet<QString> paths;
        for (const QString& path : record.game_paths) {
            const QString trimmed = path.trimmed();
            if (!trimmed.isEmpty()) {
                paths.insert(trimmed);
            }
        }

        m_order.append(name);
        m_members.insert(name, std::move(paths));
    }
}

void GameCategories::Save() const {
    if (!m_info_cache) {
        return;
    }

    std::vector<CategoryRecord> records;
    records.reserve(m_order.size());

    for (const QString& name : m_order) {
        CategoryRecord record;
        record.name = name;
        // Sorted so the stored rows stay stable between runs.
        record.game_paths = QStringList(m_members[name].values());
        record.game_paths.sort();
        records.push_back(std::move(record));
    }

    m_info_cache->SaveCategories(records);
}

QString GameCategories::Resolve(const QString& category) const {
    const QString trimmed = category.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    for (const QString& name : m_order) {
        if (name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return name;
        }
    }
    return {};
}

bool GameCategories::Contains(const QString& category) const {
    return !Resolve(category).isEmpty();
}

bool GameCategories::Create(const QString& category) {
    const QString trimmed = category.trimmed();
    if (trimmed.isEmpty() || Contains(trimmed)) {
        return false;
    }

    m_order.append(trimmed);
    m_members.insert(trimmed, {});

    Save();
    Q_EMIT Changed();
    return true;
}

bool GameCategories::Rename(const QString& old_name, const QString& new_name) {
    const QString existing = Resolve(old_name);
    const QString trimmed = new_name.trimmed();

    if (existing.isEmpty() || trimmed.isEmpty()) {
        return false;
    }
    if (existing == trimmed) {
        return true;
    }
    // Allow changing only the casing of the very same category.
    if (Contains(trimmed) && Resolve(trimmed) != existing) {
        return false;
    }

    const int index = m_order.indexOf(existing);
    if (index < 0) {
        return false;
    }

    m_order[index] = trimmed;
    m_members.insert(trimmed, m_members.take(existing));

    Save();
    Q_EMIT Changed();
    return true;
}

bool GameCategories::Remove(const QString& category) {
    const QString existing = Resolve(category);
    if (existing.isEmpty()) {
        return false;
    }

    m_order.removeAll(existing);
    m_members.remove(existing);

    Save();
    Q_EMIT Changed();
    return true;
}

QStringList GameCategories::CategoriesOf(const GameKey& key) const {
    QStringList result;
    if (key.IsNull()) {
        return result;
    }

    for (const QString& name : m_order) {
        if (IsInCategory(key, name)) {
            result.append(name);
        }
    }
    return result;
}

bool GameCategories::IsInCategory(const GameKey& key, const QString& category) const {
    const QString existing = Resolve(category);
    if (existing.isEmpty() || key.IsNull()) {
        return false;
    }

    const auto it = m_members.constFind(existing);
    return it != m_members.constEnd() && it->contains(key.path);
}

bool GameCategories::ApplyMembership(const GameKey& key, const QString& resolved_category,
                                     bool member) {
    QSet<QString>& paths = m_members[resolved_category];

    if (!member) {
        return paths.remove(key.path);
    }
    if (paths.contains(key.path)) {
        return false;
    }
    paths.insert(key.path);
    return true;
}

void GameCategories::SetMembership(const GameKey& key, const QString& category, bool member) {
    const QString existing = Resolve(category);
    if (existing.isEmpty() || key.IsNull()) {
        return;
    }

    if (!ApplyMembership(key, existing, member)) {
        return;
    }

    Save();
    Q_EMIT Changed();
}

bool GameCategories::MoveTo(const GameKey& key, const QString& category) {
    if (key.IsNull()) {
        return false;
    }

    // An empty target means "no category at all".
    QString target;
    if (!category.trimmed().isEmpty()) {
        target = Resolve(category);
        if (target.isEmpty()) {
            return false;
        }
    }

    bool changed = false;
    for (const QString& name : m_order) {
        changed |= ApplyMembership(key, name, name == target);
    }

    if (!changed) {
        return false;
    }

    Save();
    Q_EMIT Changed();
    return true;
}

bool GameCategories::MoveBetween(const GameKey& key, const QString& from, const QString& to) {
    if (key.IsNull()) {
        return false;
    }

    const QString source = Resolve(from);
    const QString target = Resolve(to);
    if (source.isEmpty() || target.isEmpty() || source == target) {
        return false;
    }

    bool changed = ApplyMembership(key, source, false);
    changed |= ApplyMembership(key, target, true);

    if (!changed) {
        return false;
    }

    Save();
    Q_EMIT Changed();
    return true;
}

bool GameCategories::Relocate(const QString& old_path, const QString& new_path) {
    const QString from = NormalizedPath(old_path);
    const QString to = NormalizedPath(new_path);
    if (from.isEmpty() || to.isEmpty() || from == to) {
        return false;
    }

    bool changed = false;
    for (const QString& name : m_order) {
        QSet<QString>& paths = m_members[name];
        if (paths.remove(from)) {
            paths.insert(to);
            changed = true;
        }
    }

    if (!changed) {
        return false;
    }

    Save();
    Q_EMIT Changed();
    return true;
}

int GameCategories::CountIn(const QString& category) const {
    const QString existing = Resolve(category);
    if (existing.isEmpty()) {
        return 0;
    }

    const auto it = m_members.constFind(existing);
    return it == m_members.constEnd() ? 0 : static_cast<int>(it->size());
}
