#include "test_coordinator.hpp"

void TestGdbCoordinator::initTestCase() {
}

void TestGdbCoordinator::testInitialRankStates() {
    gridlock::GdbRankCoordinator coordinator;
    
    QCOMPARE(coordinator.getProcessCount(), 0);
    
    gridlock::RankState state = coordinator.getRankState(0);
    QCOMPARE(state.currentState, QString("offline"));
}

void TestGdbCoordinator::cleanupTestCase() {
}

QTEST_MAIN(TestGdbCoordinator)
