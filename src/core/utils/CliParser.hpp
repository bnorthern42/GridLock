/**
 * @file CliParser.hpp
 * @brief Command-line option parser and headless platform detector for GridLock.
 *
 * Wraps QCommandLineParser to provide strongly-typed queries for --test-mode,
 * --tutorial-mode, --verbose, --headless, and --snapshot <file>, as well as a
 * pre-QApplication static helper to determine whether the offscreen QPA plugin
 * should be enabled.
 */

#pragma once

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace gridlock {
namespace core {
namespace utils {

/**
 * @struct CliOptions
 * @brief Parsed command-line target path, inferior arguments (`-- <args...>`), and mode flags.
 */
struct CliOptions {
    QString targetPath;       ///< Positional binary, source file, or workspace directory path.
    QStringList programArgs;  ///< Trailing inferior arguments captured after `--`.
    bool tutorialMode = false;///< True when `--tutorial-mode` is set.
    bool testMode = false;    ///< True when `--test-mode` is set.
    bool verbose = false;     ///< True when `--verbose` is set.
    bool headless = false;    ///< True when `--headless`, `--snapshot`, or `--verify-plugin` is set.
    QString snapshotPath;     ///< Output PNG path when `--snapshot <file>` is set.
    QString verifyPluginPath; ///< Plugin shared library path when `--verify-plugin <path>` is set.
    QString exportMiTracePath;///< Output file path when `--export-mi-trace <file>` is set.
};

/**
 * @brief Parses command-line arguments and exposes runtime launch mode flags.
 */
class CliParser {
public:
    /** @brief Initializes all supported CLI flags and positional arguments. */
    CliParser();

    /**
     * @brief Parses a list of command-line arguments without exiting on error.
     * @param args Argument list starting with the executable name at index 0.
     * @return True if all options were parsed validly; false otherwise.
     */
    bool parse(const QStringList& args);

    /**
     * @brief Processes command-line arguments bound to a live QCoreApplication instance.
     * @param app Active QCoreApplication or QApplication instance.
     */
    void process(const QCoreApplication& app);

    /** @brief Returns the structured CLI options snapshot populated by parse()/process(). */
    const CliOptions& options() const { return m_options; }

    /** @brief Returns true if --tutorial-mode was passed on the command line. */
    bool isTutorialMode() const;

    /** @brief Returns true if --test-mode was passed on the command line. */
    bool isTestMode() const;

    /** @brief Returns true if --verbose was passed on the command line. */
    bool isVerbose() const;

    /** @brief Returns true if --headless or --snapshot was passed on the command line. */
    bool isHeadless() const;

    /** @brief Returns the target output file path supplied to --snapshot, or empty if unset. */
    QString snapshotFilePath() const;

    /** @brief Returns the plugin shared library path supplied to `--verify-plugin`, or empty if unset. */
    QString verifyPluginPath() const;

    /** @brief Returns the output file path supplied to `--export-mi-trace`, or empty if unset. */
    QString exportMiTracePath() const;

    /** @brief Returns the positional binary, source file, or workspace directory path. */
    QString targetPath() const;

    /** @brief Returns the first positional source/binary file argument, or empty if none. */
    QString getTestFilePath() const;

    /** @brief Returns inferior command-line arguments supplied after `--` (or extra positionals). */
    QStringList programArgs() const { return m_options.programArgs; }

    /** @brief Formats `programArgs()` as a shell-quoted argument string for `ProjectSettings`. */
    QString programArgsString() const;

    /**
     * @brief Inspects raw main() argv strings prior to QApplication construction.
     * @param argc Argument count passed to main().
     * @param argv Argument vector passed to main().
     * @return True ONLY when --headless or --snapshot is explicitly present in argv before `--`.
     */
    static bool shouldUseOffscreenPlatform(int argc, char* argv[]);

    /** @brief Returns a mutable reference to the underlying QCommandLineParser. */
    QCommandLineParser& getParser();

private:
    void syncParsedOptions(const QStringList& explicitProgramArgs, bool hadDashSeparator);

    QCommandLineParser m_parser;
    QCommandLineOption m_testModeOption;
    QCommandLineOption m_tutorialModeOption;
    QCommandLineOption m_verboseOption;
    QCommandLineOption m_headlessOption;
    QCommandLineOption m_snapshotOption;
    QCommandLineOption m_verifyPluginOption;
    QCommandLineOption m_exportMiTraceOption;
    CliOptions m_options;
};

} // namespace utils
} // namespace core
} // namespace gridlock
