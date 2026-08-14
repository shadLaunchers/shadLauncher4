// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

struct GameInfo;
class GUISettings;

struct GameKey {
    QString path;

    bool IsNull() const {
        return path.isEmpty();
    }
};

class GameCategories : public QObject {
    Q_OBJECT

public:
    explicit GameCategories(std::shared_ptr<GUISettings> gui_settings, QObject* parent = nullptr);

    /** Key of a scanned game. Paths are normalized the same way the game
     *  scanner normalizes them, so keys stay comparable. */
    static GameKey KeyFor(const GameInfo& info);
    /** Normalizes a path the same way the game scanner does. */
    static QString NormalizedPath(const QString& path);

    /** Reload the categories from the settings, discarding anything in memory. */
    void Load();
    /** Write the categories back to the settings. */
    void Save() const;

    /** Category names in user defined (insertion) order. */
    const QStringList& Names() const {
        return m_order;
    }
    /** Case insensitive lookup of an existing category. Returns the stored
     *  spelling, or an empty string when there is no such category. */
    QString Resolve(const QString& category) const;
    bool Contains(const QString& category) const;

    /** Create an empty category. Returns false if the name is empty or taken. */
    bool Create(const QString& category);
    bool Rename(const QString& old_name, const QString& new_name);
    bool Remove(const QString& category);

    /** Categories the given game belongs to, in category order. */
    QStringList CategoriesOf(const GameKey& key) const;
    bool IsInCategory(const GameKey& key, const QString& category) const;
    /** Add or remove a single game from a category. The category has to exist. */
    void SetMembership(const GameKey& key, const QString& category, bool member);
    /** Put a game into exactly one category, dropping every other membership it
     *  had. An empty category leaves the game uncategorized. Returns true when
     *  something actually changed. */
    bool MoveTo(const GameKey& key, const QString& category);
    /** Move a game out of one category and into another, leaving any unrelated
     *  categories it belongs to untouched. */
    bool MoveBetween(const GameKey& key, const QString& from, const QString& to);

    /** Follow a game that changed location (a ZArchive conversion, for
     *  instance) so its category assignments survive the move. */
    bool Relocate(const QString& old_path, const QString& new_path);

    /** Number of games assigned to a category, installed or not. */
    int CountIn(const QString& category) const;

Q_SIGNALS:
    /** Emitted whenever categories or memberships changed and were saved. */
    void Changed();

private:
    /** Membership change on an already resolved category, without saving or
     *  notifying. Returns true when the category actually changed. */
    bool ApplyMembership(const GameKey& key, const QString& resolved_category, bool member);

    std::shared_ptr<GUISettings> m_gui_settings;
    QStringList m_order;                     // category names, ordered
    QHash<QString, QSet<QString>> m_members; // category name -> game paths
};
