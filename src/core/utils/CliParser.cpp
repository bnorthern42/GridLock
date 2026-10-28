#include "CliParser.hpp"
#include "../Version.hpp"

namespace gridlock {
namespace core {
namespace utils {

CliParser::CliParser() 
    : m_testModeOption("test-mode", "Run interactive visual simulation tests")
    , m_tutorialModeOption("tutorial-mode", "Launch the GridLock interactive tutorial suite")
    , m_verboseOption("verbose", "Enable verbose debug output")
    , m_headlessOption("headless", "Run using the offscreen platform without mapping a visible desktop window")
    , m_snapshotOption("snapshot", "Render the main window workspace and save a PNG snapshot to <file>, then exit.", "file")
{
    m_parser.setApplicationDescription(
        QString("GridLock MPI Debugger - v%1\nFeaturing the new Zero-Copy Multi-Rank "
        "Domain Heatmap!").arg(gridlock::core::Version::getString()));
    m_parser.addHelpOption();
    m_parser.addVersionOption();
    
    m_parser.addPositionalArgument("file", "File to compile and test", "[file]");
    
    m_parser.addOption(m_testModeOption);
    m_parser.addOption(m_tutorialModeOption);
    m_parser.addOption(m_verboseOption);
    m_parser.addOption(m_headlessOption);
    m_parser.addOption(m_snapshotOption);
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

bool CliParser::isVerbose() const {
    return m_parser.isSet(m_verboseOption);
}

bool CliParser::isHeadless() const {
    return m_parser.isSet(m_headlessOption) || m_parser.isSet(m_snapshotOption);
}

QString CliParser::snapshotFilePath() const {
    return m_parser.value(m_snapshotOption);
}

bool CliParser::shouldUseOffscreenPlatform(int argc, char* argv[]) {
    if (!argv) {
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) {
            continue;
        }
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--headless") ||
            arg == QLatin1String("--snapshot") ||
            arg.startsWith(QLatin1String("--snapshot="))) {
            return true;
        }
    }
    return false;
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
