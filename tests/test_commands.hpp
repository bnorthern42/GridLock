/**
 * @file test_commands.hpp
 * @brief Unit test fixture for command pattern debugger abstractions (IDebugCommand).
 *
 * Verifies polymorphic command dispatching for StepCommand and PauseCommand
 * against coordinator mock interfaces.
 */

#pragma once

#include <QObject>
#include <QtTest>

/**
 * @class TestCommands
 * @brief QtTest suite verifying debugger command execution.
 */
class TestCommands : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that StepCommand dispatches stepAll() across processes.
     */
    void testStepCommand();

    /**
     * @brief Verifies that PauseCommand pauses the designated rank (converting 1-based index).
     */
    void testPauseCommand();
};
