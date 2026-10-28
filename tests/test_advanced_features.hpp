/**
 * @file test_advanced_features.hpp
 * @brief Unit test fixture for advanced debugging features.
 *
 * Verifies deadlock analyzer pattern detection, floating-point exception (SIGFPE)
 * traps, conditional breakpoint syntax emission, and variable value change highlighting.
 */

#pragma once

#include <QObject>

/**
 * @class TestAdvancedFeatures
 * @brief QtTest suite covering deadlock analysis, FPE handling, and variable diffing.
 */
class TestAdvancedFeatures : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that DeadlockAnalyzer detects blocking MPI primitives (Barrier, Wait).
     */
    void testDeadlockAnalyzer();

    /**
     * @brief Verifies that FPE trapping broadcasts SIGFPE signal catches to GDB.
     */
    void testFpeTrapper();

    /**
     * @brief Verifies that conditional breakpoints format `-break-insert -c` commands properly.
     */
    void testConditionalBreakpoints();

    /**
     * @brief Verifies that VariableTreeModel applies color highlights when variable values change.
     */
    void testValueChangeHighlighting();
};
