#pragma once

#include <QCommandLineParser>
#include <QStringList>
#include <QString>

namespace gridlock {
namespace core {
namespace utils {

class CliParser {
public:
    CliParser();
    
    bool parse(const QStringList& args);
    void process(const QCoreApplication& app);

    bool isTutorialMode() const;
    bool isTestMode() const;
    QString getTestFilePath() const;
    
    QCommandLineParser& getParser();

private:
    QCommandLineParser m_parser;
    QCommandLineOption m_testModeOption;
    QCommandLineOption m_tutorialModeOption;
};

} // namespace utils
} // namespace core
} // namespace gridlock
