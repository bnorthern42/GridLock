/**
 * @file GridLockApp.hpp
 * @brief Top-level application coordinator object for GridLock.
 *
 * Defines the core application lifecycle coordinator encapsulating subsystem
 * initialization, runtime signal routing, and application shutdown handlers.
 */

#pragma once

#include <QObject>

namespace gridlock {

/**
 * @class GridLockApp
 * @brief High-level application context manager for GridLock.
 *
 * Coordinates lifetime management for core services, background runners,
 * and top-level Qt application events.
 */
class GridLockApp : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the application context object.
     * @param parent Optional parent QObject.
     */
    explicit GridLockApp(QObject* parent = nullptr);

    /**
     * @brief Virtual destructor for clean resource release.
     */
    ~GridLockApp() override = default;
};

}  // namespace gridlock
