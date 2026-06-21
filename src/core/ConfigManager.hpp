#pragma once
#include <QString>
#include <QSettings>
#include <toml++/toml.h>
#include <QMap>
#include <QSet>

namespace gridlock::core {

/// Canonical storage for MPI/GDB configuration.
/// QSettings ("GridLock"/"Debugger") is the live layer;
/// the TOML file holds theme colours and breakpoints.
struct DebuggerSettings {
    QString gdbPath      = "gdb";
    QString mpiExecutable = "mpiexec";
    QString mpiArgs      = "--oversubscribe";
    int     defaultRanks = 2;
};

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager instance;
        return instance;
    }

    void loadConfig();

    // ── Theme / colour accessors (TOML-backed) ───────────────────────────
    QString getSourceBackground() const;
    QString getSourceText() const;
    QString getSourceActiveLine() const;

    QString getAssemblyOpcode() const;
    QString getAssemblyRegister() const;
    QString getAssemblyAddress() const;

    // ── Debugger settings (QSettings-backed, single source of truth) ─────
    DebuggerSettings getDebuggerSettings() const;
    void             saveDebuggerSettings(const DebuggerSettings &s);

    /// Convenience shims used by existing call-sites.
    int     getDefaultRanks()   const;
    QString getGdbPath()        const;
    QString getMpiExecutable()  const;
    QString getMpiArgs()        const;

    /// Persist the last directory visited by a file-open dialog so the IDE
    /// restores it across launches.
    QString getLastOpenDir()    const;
    void    setLastOpenDir(const QString &path);

    // ── Breakpoints (TOML-backed) ─────────────────────────────────────────
    QMap<QString, QSet<int>> getBreakpoints() const;
    void saveBreakpoints(const QMap<QString, QSet<int>>& breakpoints);

private:
    ConfigManager();
    ~ConfigManager() = default;

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    toml::table m_config;

    static constexpr const char* kOrg = "GridLock";
    static constexpr const char* kApp = "Debugger";
};

} // namespace gridlock::core
