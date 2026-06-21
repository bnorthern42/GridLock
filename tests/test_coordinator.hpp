#pragma once
#include <QtTest>
#include "../src/GdbRankCoordinator.hpp"

class TestGdbCoordinator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialRankStates();
    void testDataDrivenParser_data();
    void testDataDrivenParser();
    void testMultiRankSync();
    void testBreakpointCacheFlush();
    void testRecreateWatchVariable();
    void cleanupTestCase();
};
