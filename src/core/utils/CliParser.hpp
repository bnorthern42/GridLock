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

    /** @brief Returns the first positional source/binary file argument, or empty if none. */
    QString getTestFilePath() const;

    /**
     * @brief Inspects raw main() argv strings prior to QApplication construction.
     * @param argc Argument count passed to main().
     * @param argv Argument vector passed to main().
     * @return True ONLY when --headless or --snapshot is explicitly present in argv.
     */
    static bool shouldUseOffscreenPlatform(int argc, char* argv[]);

    /** @brief Returns a mutable reference to the underlying QCommandLineParser. */
    QCommandLineParser& getParser();

private:
    QCommandLineParser m_parser;
    QCommandLineOption m_testModeOption;
    QCommandLineOption m_tutorialModeOption;
    QCommandLineOption m_verboseOption;
    QCommandLineOption m_headlessOption;
    QCommandLineOption m_snapshotOption;
};

} // namespace utils
} // namespace core
} // namespace gridlock
