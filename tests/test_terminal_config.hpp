#ifndef TEST_TERMINAL_CONFIG_HPP
#define TEST_TERMINAL_CONFIG_HPP

#include <QObject>
#include <QtTest/QtTest>

class TestTerminalConfig : public QObject {
    Q_OBJECT
private slots:
    void testFontConfiguration();
    void testColorSchemeConfiguration();
    void testScrollbackLimit();
    void testEnvironmentVariables();
};

#endif // TEST_TERMINAL_CONFIG_HPP
