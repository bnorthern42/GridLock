/**
 * @file test_vulkan_backend.hpp
 * @brief Unit tests for headless Vulkan instance initialization and surface creation.
 *
 * Verifies that Qt's Vulkan abstraction layer can initialize an instance and
 * instantiate a Vulkan surface window offscreen.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestVulkanBackend
 * @brief Test suite validating offscreen Vulkan instance initialization.
 */
class TestVulkanBackend : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies creation of QVulkanInstance and offscreen QWindow surface.
     */
    void testOffscreenVulkan();
};
