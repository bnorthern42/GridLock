#pragma once
#include <QtTest>

class TestVulkanBackend : public QObject {
    Q_OBJECT

private slots:
    void testOffscreenVulkan();
};
