/**
 * @file test_terminal_config.cpp
 * @brief Implementation of terminal dock configuration unit tests.
 *
 * Validates font preferences, color scheme options, scrollback line count limits,
 * and inherited shell environment variables in TerminalDockWidget instances.
 */

#include "test_terminal_config.hpp"

#include <QApplication>
#include <QFont>
#include <QStringList>
#include <QtTest/QtTest>

#include "../src/ui/docks/TerminalDockWidget.hpp"

using namespace gridlock::ui;

/**
 * @brief Tests setting and retrieving font family and point size on TerminalDockWidget.
 */
void TestTerminalConfig::testFontConfiguration() {
    TerminalDockWidget terminal(nullptr);

    // Apply custom font configuration.
    terminal.setTerminalFont("FiraCode Nerd Font", 12);

    // Verify applied font matches requested family and size.
    QFont appliedFont = terminal.terminalFont();
    QCOMPARE(appliedFont.family(), QString("FiraCode Nerd Font"));
    QCOMPARE(appliedFont.pointSize(), 12);
}

/**
 * @brief Tests setting and retrieving terminal color scheme.
 */
void TestTerminalConfig::testColorSchemeConfiguration() {
    TerminalDockWidget terminal(nullptr);

    // Apply common scheme name.
    terminal.setColorScheme("DarkPastels"); // A common qtermwidget scheme
    QCOMPARE(terminal.colorScheme(), QString("DarkPastels"));
}

/**
 * @brief Tests setting and retrieving terminal scrollback buffer limit.
 */
void TestTerminalConfig::testScrollbackLimit() {
    TerminalDockWidget terminal(nullptr);

    // Configure scrollback history depth to 5,000 lines.
    terminal.setScrollbackLines(5000);
    QCOMPARE(terminal.scrollbackLines(), 5000);
}

/**
 * @brief Tests assigning custom environment variables to child terminal processes.
 */
void TestTerminalConfig::testEnvironmentVariables() {
    TerminalDockWidget terminal(nullptr);

    // Specify color and terminal type environment variables.
    QStringList env;
    env << "TERM=xterm-256color" << "COLORTERM=truecolor";
    terminal.setEnvironment(env);

    // Verify configured environment variables are preserved.
    QStringList appliedEnv = terminal.environment();
    QVERIFY(appliedEnv.contains("TERM=xterm-256color"));
    QVERIFY(appliedEnv.contains("COLORTERM=truecolor"));
}

QTEST_MAIN(TestTerminalConfig)
