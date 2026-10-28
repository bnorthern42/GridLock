/**
 * @file CliParser.cpp
 * @brief Implementation of command-line flag registration and offscreen detection.
 *
 * Registers GridLock's CLI options (--test-mode, --tutorial-mode, --verbose,
 * --headless, --snapshot) on QCommandLineParser and provides a pre-QApplication
 * raw argv scanner so normal launches never enter offscreen mode unintentionally.
 */

#include "CliParser.hpp"

#include "../Version.hpp"

namespace gridlock {
namespace core {
namespace utils {

/**
 * @brief Constructs CliParser and registers all supported GridLock CLI flags.
 */
CliParser::CliParser()
    : m_testModeOption("test-mode", "Run interactive visual simulation tests")
    , m_tutorialModeOption("tutorial-mode", "Launch the GridLock interactive tutorial suite")
    , m_verboseOption("verbose", "Enable verbose debug output")
    , m_headlessOption("headless", "Run using the offscreen platform without mapping a visible desktop window")
    , m_snapshotOption("snapshot", "Render the main window workspace and save a PNG snapshot to <file>, then exit.", "file")
{
    // Configure the CLI help banner with the current semantic version string.
    m_parser.setApplicationDescription(
        QString("GridLock MPI Debugger - v%1\nFeaturing the new Zero-Copy Multi-Rank "
                "Domain Heatmap!").arg(gridlock::core::Version::getString()));
    m_parser.addHelpOption();
    m_parser.addVersionOption();

    // Register optional positional target source/binary path argument.
    m_parser.addPositionalArgument("file", "File to compile and test", "[file]");

    // Register all boolean and value-taking CLI options.
    m_parser.addOption(m_testModeOption);
    m_parser.addOption(m_tutorialModeOption);
    m_parser.addOption(m_verboseOption);
    m_parser.addOption(m_headlessOption);
    m_parser.addOption(m_snapshotOption);
}

/**
 * @brief Parses @p args without exiting on error (used by unit test suites).
 */
bool CliParser::parse(const QStringList& args) {
    // Non-exiting parse used by unit tests to validate option combinations.
    return m_parser.parse(args);
}

/**
 * @brief Processes CLI arguments from @p app and handles `--help` / `--version`.
 */
void CliParser::process(const QCoreApplication& app) {
    // Standard application option processing (handles --help and --version automatically).
    m_parser.process(app);
}

/** @brief Returns `true` if `--tutorial-mode` was passed. */
bool CliParser::isTutorialMode() const {
    return m_parser.isSet(m_tutorialModeOption);
}

/** @brief Returns `true` if `--test-mode` was passed. */
bool CliParser::isTestMode() const {
    return m_parser.isSet(m_testModeOption);
}

/** @brief Returns `true` if `--verbose` was passed. */
bool CliParser::isVerbose() const {
    return m_parser.isSet(m_verboseOption);
}

/** @brief Returns `true` if `--headless` or `--snapshot` was passed. */
bool CliParser::isHeadless() const {
    // Both --headless and --snapshot imply headless non-interactive execution.
    return m_parser.isSet(m_headlessOption) || m_parser.isSet(m_snapshotOption);
}

/** @brief Returns the output PNG path passed to `--snapshot <file>`. */
QString CliParser::snapshotFilePath() const {
    return m_parser.value(m_snapshotOption);
}

/**
 * @brief Inspects raw `argv` prior to `QApplication` creation for `--headless` or `--snapshot`.
 */
bool CliParser::shouldUseOffscreenPlatform(int argc, char* argv[]) {
    if (!argv) {
        return false;
    }
    // Scan raw argv tokens starting at index 1 before QApplication initializes platform plugins.
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

/** @brief Returns the optional positional target file path, or an empty string. */
QString CliParser::getTestFilePath() const {
    // Return the first positional argument if one was supplied on the command line.
    const QStringList& posArgs = m_parser.positionalArguments();
    if (!posArgs.isEmpty()) {
        return posArgs.first();
    }
    return QString();
}

/** @brief Returns a mutable reference to the underlying `QCommandLineParser`. */
QCommandLineParser& CliParser::getParser() {
    return m_parser;
}

}  // namespace utils
}  // namespace core
}  // namespace gridlock
