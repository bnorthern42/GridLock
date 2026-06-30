#pragma once
#include <QObject>
#include <QtTest>

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void testSerialization();
    void testDeserialization();
    void testFallbacks();
    void testMpiSettingsPersistence();
    void testSettingValidation();
    void testSlurmSettingsPersistence();
    void testSshSettingsPersistence();
    void testTerminalSettingsPersistence();
};
