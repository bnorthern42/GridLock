/**
 * @file GridLockApp.cpp
 * @brief Implementation of the top-level application coordinator.
 *
 * Implements the constructor and lifecycle routines for GridLockApp,
 * establishing foundational application-level dependencies and state.
 */

#include "GridLockApp.hpp"

namespace gridlock {

/**
 * @brief Constructs the application coordinator instance.
 * @param parent Optional parent QObject context.
 *
 * Initializes the root application object and prepares the environment
 * for window creation and background daemon orchestration.
 */
GridLockApp::GridLockApp(QObject* parent)
    : QObject(parent) {
    // Foundational application object initialization.
}

}  // namespace gridlock
