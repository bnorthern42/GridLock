#pragma once
#include <QString>
#include <toml++/toml.h>

namespace gridlock::core {

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager instance;
        return instance;
    }

    void loadConfig();

    QString getSourceBackground() const;
    QString getSourceText() const;
    QString getSourceActiveLine() const;

    QString getAssemblyOpcode() const;
    QString getAssemblyRegister() const;
    QString getAssemblyAddress() const;

    int getDefaultRanks() const;
    QString getGdbPath() const;

private:
    ConfigManager();
    ~ConfigManager() = default;

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    toml::table m_config;
};

} // namespace gridlock::core
