#pragma once
#include <QtTest>
#include "../src/GdbRankCoordinator.hpp"

class TestGdbCoordinator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialRankStates();
    void cleanupTestCase();
};
