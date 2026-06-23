#include "test_vulkan_backend.hpp"
#include <QVulkanInstance>
#include <QWindow>
#include <QVulkanFunctions>

void TestVulkanBackend::testOffscreenVulkan() {
    QVulkanInstance vulkanInstance;
    bool result = vulkanInstance.create();
    QVERIFY(result);

    QWindow surface;
    surface.setSurfaceType(QSurface::VulkanSurface);
    surface.setVulkanInstance(&vulkanInstance);
    surface.create();

    // QWindow does not have isValid()

    
    QVulkanFunctions *f = vulkanInstance.functions();
    QVERIFY(f != nullptr);
}

QTEST_MAIN(TestVulkanBackend)
