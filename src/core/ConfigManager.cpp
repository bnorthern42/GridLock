#include "ConfigManager.hpp"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <fstream>
#include <iostream>

namespace gridlock::core {

ConfigManager::ConfigManager() {
    loadConfig();
}

void ConfigManager::loadConfig() {
    QString configPath = "gridlock_config.toml";
    
    if (!QFile::exists(configPath)) {
        // Create default config
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
            out << "default_ranks = 6\n";
            out << "gdb_path = \"gdb\"\n";
            file.close();
        }
    }

    try {
        m_config = toml::parse_file(configPath.toStdString());
    } catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed:\n" << err << "\n";
    }
}

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

int ConfigManager::getDefaultRanks() const {
    return m_config["debugger"]["default_ranks"].value_or(6);
}

QString ConfigManager::getGdbPath() const {
    return QString::fromStdString(m_config["debugger"]["gdb_path"].value_or("gdb"));
}

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
                breakpoints[path] = lineSet;
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
        bpTable.insert(it.key().toStdString(), linesArr);
    }
    m_config.insert_or_assign("breakpoints", bpTable);
    
    std::ofstream out("gridlock_config.toml");
    out << m_config;
}

} // namespace gridlock::core
