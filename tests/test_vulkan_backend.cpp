/**
 * @file test_vulkan_backend.cpp
 * @brief Implementation of Vulkan hardware graphics subsystem unit tests.
 *
 * Verifies QVulkanInstance creation, Vulkan function table resolution, and
 * dummy offscreen Vulkan window surface instantiation.
 */

#include "test_vulkan_backend.hpp"

#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>
#include <QtTest/QtTest>

/**
 * @brief Verifies that an offscreen Vulkan instance and surface can be initialized.
 */
void TestVulkanBackend::testOffscreenVulkan() {
    // Instantiate Vulkan instance.
    QVulkanInstance vulkanInstance;
    bool result = vulkanInstance.create();
    QVERIFY(result);

    // Create offscreen surface with Vulkan surface type.
    QWindow surface;
    surface.setSurfaceType(QSurface::VulkanSurface);
    surface.setVulkanInstance(&vulkanInstance);
    surface.create();

    // Query Vulkan function dispatch table.
    QVulkanFunctions *f = vulkanInstance.functions();
    QVERIFY(f != nullptr);
}

QTEST_MAIN(TestVulkanBackend)
