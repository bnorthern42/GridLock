#pragma once
#include <QtTest>
#include "../src/GdbRankCoordinator.hpp"

class TestGdbCoordinator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialRankStates();
    void testGdbMiParser_data();
    void testGdbMiParser();
    void testMultiRankSync();
    void testBreakpointCacheFlush();
    void testRecreateWatchVariable();
    void testRegisterValuesParsing();
    void cleanupTestCase();
};
