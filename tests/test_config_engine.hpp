#pragma once

#include <QObject>
#include <QtTest/QtTest>

class TestConfigEngine : public QObject {
    Q_OBJECT

private slots:
    void testConfigIO();
    void testMruLogic();
};
