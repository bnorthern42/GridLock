#include "test_terminal_config.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include <QApplication>
#include <QFont>
#include <QStringList>

using namespace gridlock::ui;

void TestTerminalConfig::testFontConfiguration() {
    TerminalDockWidget terminal(nullptr);
    
    terminal.setTerminalFont("FiraCode Nerd Font", 12);
    
    QFont appliedFont = terminal.terminalFont();
    QCOMPARE(appliedFont.family(), QString("FiraCode Nerd Font"));
    QCOMPARE(appliedFont.pointSize(), 12);
}

void TestTerminalConfig::testColorSchemeConfiguration() {
    TerminalDockWidget terminal(nullptr);
    
    terminal.setColorScheme("DarkPastels"); // A common qtermwidget scheme
    QCOMPARE(terminal.colorScheme(), QString("DarkPastels"));
}

void TestTerminalConfig::testScrollbackLimit() {
    TerminalDockWidget terminal(nullptr);
    
    terminal.setScrollbackLines(5000);
    QCOMPARE(terminal.scrollbackLines(), 5000);
}

void TestTerminalConfig::testEnvironmentVariables() {
    TerminalDockWidget terminal(nullptr);
    
    QStringList env;
    env << "TERM=xterm-256color" << "COLORTERM=truecolor";
    terminal.setEnvironment(env);
    
    QStringList appliedEnv = terminal.environment();
    QVERIFY(appliedEnv.contains("TERM=xterm-256color"));
    QVERIFY(appliedEnv.contains("COLORTERM=truecolor"));
}

// Ensure the test gets executed
QTEST_MAIN(TestTerminalConfig)
