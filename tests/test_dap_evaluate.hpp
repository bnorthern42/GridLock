/**
 * @file test_dap_evaluate.hpp
 * @brief Unit tests for DAP expression evaluation and watch inspection.
 *
 * Verifies evaluate request construction with cached stack frame identifiers
 * and evaluation response parsing into expressionEvaluated signals.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapEvaluate
 * @brief Test suite exercising DAP expression evaluation workflow.
 */
class TestDapEvaluate : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that evaluate requests serialize expression and stack frame id.
     */
    void testEvaluateRequest();

    /**
     * @brief Verifies that evaluate responses emit expressionEvaluated signals.
     */
    void testEvaluateResponse();
};
