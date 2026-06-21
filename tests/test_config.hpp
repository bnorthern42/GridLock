#pragma once
#include <QObject>
#include <QtTest>

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void testSerialization();
    void testDeserialization();
    void testFallbacks();
};
