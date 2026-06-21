#include "ConfigManager.hpp"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <fstream>
#include <iostream>

namespace gridlock::core {

// ─── Construction / TOML loading ────────────────────────────────────────────

ConfigManager::ConfigManager() {
    loadConfig();
}

void ConfigManager::loadConfig() {
    QString configPath = "gridlock_config.toml";

    if (!QFile::exists(configPath)) {
        QFile file(configPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "[theme.source]\n";
            out << "background = \"#1e1e1e\"\n";
            out << "text = \"#f0f0f0\"\n";
            out << "active_line = \"#505000\"\n\n";

            out << "[theme.assembly]\n";
            out << "opcode = \"#00ff7f\"\n";
            out << "register = \"#ff7f50\"\n";
            out << "address = \"#555555\"\n\n";

            out << "[debugger]\n";
            out << "default_ranks = 2\n";
            out << "gdb_path = \"gdb\"\n";
            file.close();
        }
    }

    try {
        m_config = toml::parse_file(configPath.toStdString());
    } catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed:\n" << err << "\n";
        m_config = toml::table();
    }
}

// ─── Theme accessors (TOML) ──────────────────────────────────────────────────

QString ConfigManager::getSourceBackground() const {
    return QString::fromStdString(m_config["theme"]["source"]["background"].value_or("#1e1e1e"));
}

QString ConfigManager::getSourceText() const {
    return QString::fromStdString(m_config["theme"]["source"]["text"].value_or("#f0f0f0"));
}

QString ConfigManager::getSourceActiveLine() const {
    return QString::fromStdString(m_config["theme"]["source"]["active_line"].value_or("#505000"));
}

QString ConfigManager::getAssemblyOpcode() const {
    return QString::fromStdString(m_config["theme"]["assembly"]["opcode"].value_or("#00ff7f"));
}

QString ConfigManager::getAssemblyRegister() const {
    return QString::fromStdString(m_config["theme"]["assembly"]["register"].value_or("#ff7f50"));
}

QString ConfigManager::getAssemblyAddress() const {
    return QString::fromStdString(m_config["theme"]["assembly"]["address"].value_or("#555555"));
}

// ─── Debugger settings — QSettings-backed single source of truth ─────────────

DebuggerSettings ConfigManager::getDebuggerSettings() const {
    QSettings s(kOrg, kApp);

    // Seed defaults from TOML so the first-run experience reads the file.
    const int    tomlRanks  = m_config["debugger"]["default_ranks"].value_or(2);
    const QString tomlGdb   = QString::fromStdString(
        m_config["debugger"]["gdb_path"].value_or("gdb"));

    DebuggerSettings ds;
    ds.gdbPath       = s.value("debugger/gdb_path",   tomlGdb).toString();
    ds.mpiExecutable = s.value("debugger/mpi_executable", "mpiexec").toString();
    ds.mpiArgs       = s.value("debugger/mpi_args",   "--oversubscribe").toString();
    ds.defaultRanks  = s.value("debugger/default_ranks", tomlRanks).toInt();

    // Safety clamp: reject non-positive values stored by an old/buggy write.
    if (ds.defaultRanks < 1) ds.defaultRanks = 1;

    return ds;
}

void ConfigManager::saveDebuggerSettings(const DebuggerSettings &ds) {
    QSettings s(kOrg, kApp);
    s.setValue("debugger/gdb_path",       ds.gdbPath);
    s.setValue("debugger/mpi_executable", ds.mpiExecutable);
    s.setValue("debugger/mpi_args",       ds.mpiArgs);
    // Clamp before persisting so we never write an invalid value.
    s.setValue("debugger/default_ranks",  qMax(1, ds.defaultRanks));
    s.sync();
}

// ─── Convenience shims ───────────────────────────────────────────────────────

int     ConfigManager::getDefaultRanks()  const { return getDebuggerSettings().defaultRanks; }
QString ConfigManager::getGdbPath()       const { return getDebuggerSettings().gdbPath; }
QString ConfigManager::getMpiExecutable() const { return getDebuggerSettings().mpiExecutable; }
QString ConfigManager::getMpiArgs()       const { return getDebuggerSettings().mpiArgs; }

QString ConfigManager::getLastOpenDir() const {
    QSettings s(kOrg, kApp);
    return s.value("ui/last_open_dir", QDir::currentPath()).toString();
}

void ConfigManager::setLastOpenDir(const QString &path) {
    QSettings s(kOrg, kApp);
    s.setValue("ui/last_open_dir", path);
    s.sync();
}

// ─── Breakpoints (TOML) ──────────────────────────────────────────────────────

QMap<QString, QSet<int>> ConfigManager::getBreakpoints() const {
    QMap<QString, QSet<int>> breakpoints;
    if (auto* bps = m_config["breakpoints"].as_table()) {
        for (auto& [file, lines] : *bps) {
            if (auto* linesArr = lines.as_array()) {
                QString path = QString::fromStdString(std::string(file.str()));
                QSet<int> lineSet;
                for (auto& elem : *linesArr) {
                    if (auto* val = elem.as_integer()) {
                        lineSet.insert(val->get());
                    }
                }
                breakpoints[QFileInfo(path).absoluteFilePath()] = lineSet;
            }
        }
    }
    return breakpoints;
}

void ConfigManager::saveBreakpoints(const QMap<QString, QSet<int>>& breakpoints) {
    toml::table bpTable;
    for (auto it = breakpoints.constBegin(); it != breakpoints.constEnd(); ++it) {
        toml::array linesArr;
        for (int line : it.value()) {
            linesArr.push_back(line);
        }
        QString relPath = QDir::current().relativeFilePath(it.key());
        bpTable.insert(relPath.toStdString(), linesArr);
    }
    m_config.insert_or_assign("breakpoints", bpTable);

    std::ofstream out("gridlock_config.toml");
    out << m_config;
}

} // namespace gridlock::core
