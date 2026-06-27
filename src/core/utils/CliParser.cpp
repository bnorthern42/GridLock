#include "CliParser.hpp"

namespace gridlock {
namespace core {
namespace utils {

CliParser::CliParser() 
    : m_testModeOption("test-mode", "Run interactive visual simulation tests")
    , m_tutorialModeOption("tutorial-mode", "Launch the GridLock interactive tutorial suite")
{
    m_parser.setApplicationDescription(
        "GridLock MPI Debugger - v0.5.2\nFeaturing the new Zero-Copy Multi-Rank "
        "Domain Heatmap!");
    m_parser.addHelpOption();
    m_parser.addVersionOption();
    
    // Make the argument optional by creating an option that can take a value
    // QCommandLineOption with a value name inherently expects a value, but we can set a default or check if it has one
    // Actually in Qt, if we just want it to be optional, QCommandLineOption doesn't natively support optional values very well.
    // However, if we pass a valueName, it expects a value.
    m_parser.addPositionalArgument("file", "File to compile and test", "[file]");
    
    m_parser.addOption(m_testModeOption);
    m_parser.addOption(m_tutorialModeOption);
}

bool CliParser::parse(const QStringList& args) {
    return m_parser.parse(args);
}

void CliParser::process(const QCoreApplication& app) {
    m_parser.process(app);
}

bool CliParser::isTutorialMode() const {
    return m_parser.isSet(m_tutorialModeOption);
}

bool CliParser::isTestMode() const {
    return m_parser.isSet(m_testModeOption);
}

QString CliParser::getTestFilePath() const {
    const QStringList& posArgs = m_parser.positionalArguments();
    if (!posArgs.isEmpty()) {
        return posArgs.first();
    }
    return QString();
}

QCommandLineParser& CliParser::getParser() {
    return m_parser;
}

} // namespace utils
} // namespace core
} // namespace gridlock
