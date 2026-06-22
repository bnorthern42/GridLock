#pragma once
#include <QObject>
#include <QtTest>

class TestDapCommands : public QObject {
    Q_OBJECT
private slots:
    void testHandshake();
    void testExecutionCommands();
};
