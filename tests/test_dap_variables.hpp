/**
 * @file test_dap_variables.hpp
 * @brief Unit tests for DAP variable inspection and VariableTreeModel hierarchy.
 *
 * Verifies scope queries, child variable resolution, lazy tree expansion via
 * canFetchMore/fetchMore, O(1) parent pointer lookups, and refresh deduplication.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapVariables
 * @brief Test suite validating DAP variable trees and UI model synchronization.
 */
class TestDapVariables : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that scopes requests query locals and arguments for a frame.
     */
    void testScopesFetch();

    /**
     * @brief Verifies variables request construction for a given variable reference ID.
     */
    void testVariablesFetch();

    /**
     * @brief Verifies VariableTreeModel root population from DAP locals responses.
     */
    void testVariableTreePopulation();

    /**
     * @brief Verifies on-demand lazy child variable resolution upon node expansion.
     */
    void testVariableLazyLoading();

    /**
     * @brief Verifies performance and O(1) lookup behavior over 10,000 variable nodes.
     */
    void testVariableTreePerformance();

    /**
     * @brief Verifies deduplication of concurrent fetchMore requests on the same node.
     */
    void testStepVariableRefresh();
};
