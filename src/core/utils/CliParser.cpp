/**
 * @file CliParser.cpp
 * @brief Implementation of command-line flag registration and offscreen detection.
 *
 * Registers GridLock's CLI options (--test-mode, --tutorial-mode, --verbose,
 * --headless, --snapshot) on QCommandLineParser and provides a pre-QApplication
 * raw argv scanner so normal launches never enter offscreen mode unintentionally.
 */

#include "CliParser.hpp"

#include <QCoreApplication>

#include "../Version.hpp"

namespace gridlock {
namespace core {
namespace utils {

namespace {
void splitAtDashSeparator(const QStringList& args, QStringList* outBefore,
                          QStringList* outAfter, bool* outHadSeparator) {
    const int dashIdx = args.indexOf(QStringLiteral("--"), 1);
    if (dashIdx >= 0) {
        *outBefore = args.mid(0, dashIdx);
        *outAfter = args.mid(dashIdx + 1);
        *outHadSeparator = true;
    } else {
        *outBefore = args;
        outAfter->clear();
        *outHadSeparator = false;
    }
}
}  // namespace

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

    // Register optional positional target binary/workspace/source argument and `-- <args...>`.
    m_parser.addPositionalArgument("target", "Target binary, source file, or workspace directory", "[target] [-- <program_args...>]");

    // Register all boolean and value-taking CLI options.
    m_parser.addOption(m_testModeOption);
    m_parser.addOption(m_tutorialModeOption);
    m_parser.addOption(m_verboseOption);
    m_parser.addOption(m_headlessOption);
    m_parser.addOption(m_snapshotOption);
}

/** @brief Synchronizes `m_options` after `m_parser.parse()` or `m_parser.process()`. */
void CliParser::syncParsedOptions(const QStringList& explicitProgramArgs, bool hadDashSeparator) {
    const QStringList posArgs = m_parser.positionalArguments();
    m_options.targetPath = posArgs.isEmpty() ? QString() : posArgs.first();
    m_options.programArgs = hadDashSeparator ? explicitProgramArgs
                                             : (posArgs.size() > 1 ? posArgs.mid(1) : QStringList{});
    m_options.tutorialMode = isTutorialMode();
    m_options.testMode = isTestMode();
    m_options.verbose = isVerbose();
    m_options.headless = isHeadless();
    m_options.snapshotPath = snapshotFilePath();
}

/**
 * @brief Parses @p args without exiting on error (used by unit test suites).
 */
bool CliParser::parse(const QStringList& args) {
    QStringList beforeDash, afterDash;
    bool hadSep = false;
    splitAtDashSeparator(args, &beforeDash, &afterDash, &hadSep);
    const bool ok = m_parser.parse(beforeDash);
    if (ok) syncParsedOptions(afterDash, hadSep);
    return ok;
}

/**
 * @brief Processes CLI arguments from @p app and handles `--help` / `--version`.
 */
void CliParser::process(const QCoreApplication& app) {
    QStringList beforeDash, afterDash;
    bool hadSep = false;
    splitAtDashSeparator(app.arguments(), &beforeDash, &afterDash, &hadSep);
    m_parser.process(beforeDash);
    syncParsedOptions(afterDash, hadSep);
}

/** @brief Returns `true` if `--tutorial-mode` was passed. */
bool CliParser::isTutorialMode() const { return m_parser.isSet(m_tutorialModeOption); }

/** @brief Returns `true` if `--test-mode` was passed. */
bool CliParser::isTestMode() const { return m_parser.isSet(m_testModeOption); }

/** @brief Returns `true` if `--verbose` was passed. */
bool CliParser::isVerbose() const { return m_parser.isSet(m_verboseOption); }

/** @brief Returns `true` if `--headless` or `--snapshot` was passed. */
bool CliParser::isHeadless() const {
    return m_parser.isSet(m_headlessOption) || m_parser.isSet(m_snapshotOption);
}

/** @brief Returns the output PNG path passed to `--snapshot <file>`. */
QString CliParser::snapshotFilePath() const { return m_parser.value(m_snapshotOption); }

/**
 * @brief Inspects raw `argv` prior to `QApplication` creation for `--headless` or `--snapshot`.
 */
bool CliParser::shouldUseOffscreenPlatform(int argc, char* argv[]) {
    if (!argv) return false;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--")) break;
        if (arg == QLatin1String("--headless") || arg == QLatin1String("--snapshot") ||
            arg.startsWith(QLatin1String("--snapshot="))) {
            return true;
        }
    }
    return false;
}

/** @brief Returns the optional positional target binary/workspace/source path. */
QString CliParser::targetPath() const {
    if (!m_options.targetPath.isEmpty()) return m_options.targetPath;
    const QStringList posArgs = m_parser.positionalArguments();
    return posArgs.isEmpty() ? QString() : posArgs.first();
}

/** @brief Returns the optional positional target file path, or an empty string. */
QString CliParser::getTestFilePath() const { return targetPath(); }

/** @brief Formats `programArgs()` as a shell-safe argument string preserving spaces/quotes. */
QString CliParser::programArgsString() const {
    QStringList formatted;
    formatted.reserve(m_options.programArgs.size());
    for (const QString& tok : m_options.programArgs) {
        if (tok.isEmpty() || tok.contains(QLatin1Char(' ')) || tok.contains(QLatin1Char('\t')) || tok.contains(QLatin1Char('"'))) {
            QString esc = tok;
            esc.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
            formatted.append(QStringLiteral("\"%1\"").arg(esc));
        } else {
            formatted.append(tok);
        }
    }
    return formatted.join(QLatin1Char(' '));
}

/** @brief Returns a mutable reference to the underlying `QCommandLineParser`. */
QCommandLineParser& CliParser::getParser() { return m_parser; }

}  // namespace utils
}  // namespace core
}  // namespace gridlock
