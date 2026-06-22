#pragma once
#include <QObject>
#include <QtTest>

class TestDapLifecycle : public QObject {
    Q_OBJECT
private slots:
    void testOutputEvent();
    void testDisconnectRequest();
};
