#pragma once
#include <QObject>
#include <QtTest>

class TestDapBreakpoints : public QObject {
    Q_OBJECT
private slots:
    void testBreakpointAccumulation();
    void testStopEventParsing();
};
