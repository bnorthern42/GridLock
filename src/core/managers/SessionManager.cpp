/**
 * @file SessionManager.cpp
 * @brief Implementation of TOML session serialization, deserialization, and MRU history.
 *
 * Uses `toml++` to encode and decode the `[workspace]` table containing
 * `watched_variables`, `active_breakpoints`, and `selected_ranks`.
 */

#include "SessionManager.hpp"

#include <fstream>
#include <iostream>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <toml++/toml.hpp>

namespace gridlock::core::managers {

/**
 * @brief Constructs SessionManager and loads persisted recent sessions from debugger.conf.
 */
SessionManager::SessionManager(QObject* parent) : QObject(parent) {
    loadRecentSessionsFromDisk();
}

/**
 * @brief Writes the `[workspace]` TOML table containing watches, breakpoints, and ranks to disk.
 */
bool SessionManager::saveSession(const QString& filePath, const SessionState& state) {
    try {
        // Populate the watched_variables TOML array from the session snapshot.
        toml::array watches;
        for (const auto& w : state.watchedVariables) {
            watches.push_back(w.toStdString());
        }

        // Populate the active_breakpoints TOML array.
        toml::array breakpoints;
        for (const auto& b : state.activeBreakpoints) {
            breakpoints.push_back(b.toStdString());
        }

        // Populate the selected_ranks TOML integer array.
        toml::array ranks;
        for (int r : state.selectedRanks) {
            ranks.push_back(r);
        }

        // Assemble the root TOML document containing the `[workspace]` section.
        toml::table tbl{
            {"workspace", toml::table{
                {"watched_variables", watches},
                {"active_breakpoints", breakpoints},
                {"selected_ranks", ranks}
            }}
        };

        // Truncate and write the serialized TOML payload to the target file path.
        std::ofstream os(filePath.toStdString(), std::ios_base::trunc);
        if (!os.is_open()) {
            qWarning() << "Failed to open file for writing:" << filePath;
            return false;
        }

        os << tbl << "\n";
        os.flush();
        // Record saved session/workspace in persisted MRU history immediately.
        addRecentSession(filePath);
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Exception while saving session:" << e.what();
        return false;
    }
}

/**
 * @brief Parses a TOML session file, updates the MRU list, and emits sessionLoaded().
 */
std::optional<SessionState> SessionManager::loadSession(const QString& filePath) {
    try {
        // Parse the TOML file from disk and verify the `[workspace]` table exists.
        auto tbl = toml::parse_file(filePath.toStdString());
        auto* workspace = tbl["workspace"].as_table();
        if (!workspace) {
            qWarning() << "No [workspace] table found in session file.";
            return std::nullopt;
        }

        SessionState state;

        // Extract watched_variables string entries.
        if (auto* watches = workspace->get_as<toml::array>("watched_variables")) {
            for (const auto& elem : *watches) {
                if (auto str = elem.value<std::string>()) {
                    state.watchedVariables.push_back(QString::fromStdString(*str));
                }
            }
        }

        // Extract active_breakpoints string entries.
        if (auto* breakpoints = workspace->get_as<toml::array>("active_breakpoints")) {
            for (const auto& elem : *breakpoints) {
                if (auto str = elem.value<std::string>()) {
                    state.activeBreakpoints.push_back(QString::fromStdString(*str));
                }
            }
        }

        // Extract selected_ranks integer entries.
        if (auto* ranks = workspace->get_as<toml::array>("selected_ranks")) {
            for (const auto& elem : *ranks) {
                if (auto val = elem.value<int>()) {
                    state.selectedRanks.push_back(*val);
                }
            }
        }

        // Resolve the enclosing workspace root directory (stepping out of `.gridlock/` if nested).
        const QFileInfo sessionInfo(filePath);
        QDir wsDir = sessionInfo.isDir() ? QDir(sessionInfo.absoluteFilePath()) : sessionInfo.dir();
        if (wsDir.dirName() == QStringLiteral(".gridlock")) {
            wsDir.cdUp();
        }
        const QFileInfo wsInfo(wsDir.absolutePath());
        const QString workspaceRoot =
            !wsInfo.canonicalFilePath().isEmpty() ? wsInfo.canonicalFilePath() : wsInfo.absoluteFilePath();

        // Record the loaded file at the front of the MRU list and notify listeners with workspaceRoot.
        addRecentSession(filePath);
        emit sessionLoaded(workspaceRoot);
        return state;
    } catch (const std::exception& e) {
        qWarning() << "Exception while loading session:" << e.what();
        return std::nullopt;
    }
}

/**
 * @brief Canonicalizes @p rawPath, collapsing `.gridlock/workspace.toml` to its workspace root.
 */
QString SessionManager::normalizeRecentPath(const QString& rawPath) {
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty() || !QFileInfo::exists(trimmed)) {
        return {};
    }
    const QFileInfo fi(trimmed);
    // Map `<ws>/.gridlock/workspace.toml` to `<ws>` so workspace switches and saves never duplicate.
    if (fi.isFile() && fi.fileName() == QStringLiteral("workspace.toml") &&
        fi.dir().dirName() == QStringLiteral(".gridlock")) {
        QDir wsDir = fi.dir();
        if (wsDir.cdUp()) {
            const QFileInfo wsInfo(wsDir.absolutePath());
            return !wsInfo.canonicalFilePath().isEmpty()
                       ? wsInfo.canonicalFilePath()
                       : QDir::cleanPath(wsInfo.absoluteFilePath());
        }
    }
    return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : QDir::cleanPath(fi.absoluteFilePath());
}

/**
 * @brief Loads recent sessions from `QSettings("gridlock", "debugger")`, merging legacy stores and TOMLs.
 */
void SessionManager::loadRecentSessionsFromDisk() {
    m_recentSessions.clear();
    auto ingestList = [this](const QStringList& rawEntries) {
        for (const QString& raw : rawEntries) {
            const QString norm = normalizeRecentPath(raw);
            if (!norm.isEmpty() && !m_recentSessions.contains(norm) &&
                m_recentSessions.size() < MAX_RECENT_SESSIONS) {
                m_recentSessions.append(norm);
            }
        }
    };

    // 1. Primary store: ~/.config/gridlock/debugger.conf via QSettings("gridlock", "debugger").
    QSettings primary(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    const QStringList primaryRaw = primary.value(QStringLiteral("recentSessions")).toStringList();
    ingestList(primaryRaw);
    ingestList(primary.value(QStringLiteral("Session/recentSessions")).toStringList());

    // 2. Legacy default QSettings() store when application/organization metadata is configured.
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    if ((!org.isEmpty() || !app.isEmpty()) && !(org == QStringLiteral("gridlock") && app == QStringLiteral("debugger"))) {
        QSettings legacy;
        ingestList(legacy.value(QStringLiteral("recentSessions")).toStringList());
    }

    // 3. Legacy ~/.config/gridlock/*.toml session files or config.toml `recent_sessions` arrays.
    const QDir cfgDir(QDir::homePath() + QStringLiteral("/.config/gridlock"));
    if (cfgDir.exists()) {
        for (const QFileInfo& tf : cfgDir.entryInfoList(QStringList{QStringLiteral("*.toml")}, QDir::Files)) {
            try {
                auto tbl = toml::parse_file(tf.absoluteFilePath().toStdString());
                if (auto* arr = tbl["recent_sessions"].as_array()) {
                    QStringList legacyTomlEntries;
                    for (const auto& elem : *arr) {
                        if (auto str = elem.value<std::string>()) legacyTomlEntries.append(QString::fromStdString(*str));
                    }
                    ingestList(legacyTomlEntries);
                }
                if (tf.fileName() != QStringLiteral("config.toml") &&
                    tf.fileName() != QStringLiteral("settings.toml") && tbl["workspace"].as_table()) {
                    ingestList(QStringList{tf.absoluteFilePath()});
                }
            } catch (...) {
                // Skip unreadable or non-session TOML files safely.
            }
        }
    }

    // Persist normalized/pruned list back to debugger.conf when pruning or legacy migration occurred.
    if (m_recentSessions != primaryRaw && !m_recentSessions.isEmpty()) {
        saveRecentSessionsToDisk();
    }
}

/**
 * @brief Writes current MRU list to `QSettings("gridlock", "debugger")` and flushes immediately.
 */
void SessionManager::saveRecentSessionsToDisk() const {
    QSettings settings(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    settings.setValue(QStringLiteral("recentSessions"), m_recentSessions);
    settings.sync();
}

/**
 * @brief Returns live persisted MRU workspace/session paths (pruning deleted entries).
 */
QStringList SessionManager::recentSessions() const {
    QStringList live;
    for (const QString& p : m_recentSessions) {
        if (QFileInfo::exists(p)) {
            live.append(p);
        }
    }
    return live;
}

/**
 * @brief Inserts @p path at index 0 of persisted MRU entries, caps at 10, and syncs debugger.conf.
 */
void SessionManager::addRecentSession(const QString& path) {
    const QString norm = normalizeRecentPath(path);
    if (norm.isEmpty()) {
        return;
    }
    // Prune deleted paths and deduplicate before prepending the active workspace/session.
    for (int i = m_recentSessions.size() - 1; i >= 0; --i) {
        if (!QFileInfo::exists(m_recentSessions.at(i))) {
            m_recentSessions.removeAt(i);
        }
    }
    m_recentSessions.removeAll(norm);
    m_recentSessions.prepend(norm);
    while (m_recentSessions.size() > MAX_RECENT_SESSIONS) {
        m_recentSessions.removeLast();
    }
    saveRecentSessionsToDisk();
}

/**
 * @brief Empties all persisted MRU session history and syncs debugger.conf.
 */
void SessionManager::clearRecentSessions() {
    m_recentSessions.clear();
    saveRecentSessionsToDisk();
}

/**
 * @brief Returns legacy or persisted MRU entries for backward-compatible callers.
 */
QStringList SessionManager::getMruSessions() const {
    return m_mruSessions.isEmpty() ? recentSessions() : m_mruSessions;
}

/**
 * @brief Inserts filePath at the front of the legacy MRU list (capped at MAX_MRU) and persists real paths.
 */
void SessionManager::addMruSession(const QString& filePath) {
    // Preserve in-memory 5-slot queue semantics for unit tests using synthetic paths.
    m_mruSessions.removeAll(filePath);
    m_mruSessions.prepend(filePath);
    if (m_mruSessions.size() > MAX_MRU) {
        m_mruSessions.removeLast();
    }
    if (QFileInfo::exists(filePath)) {
        addRecentSession(filePath);
    }
}

} // namespace gridlock::core::managers
