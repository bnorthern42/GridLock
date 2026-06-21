#pragma once
#include <QObject>
#include <QtTest>

class TestCommands : public QObject {
    Q_OBJECT
private slots:
    void testStepCommand();
    void testPauseCommand();
};
