#pragma once
#include <QVulkanWindow>
#include <vector>
#include <mutex>

namespace gridlock::ui {

class DomainHeatmapRenderer : public QVulkanWindowRenderer {
public:
    DomainHeatmapRenderer(QVulkanWindow *w);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    void startNextFrame() override;

    void uploadData(const std::vector<double>& matrix, int rows, int cols, float min_val, float max_val);

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    QVulkanWindow *m_window;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::vector<float> m_pendingUpload;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
    bool m_needsUpload = false;
    float m_minVal = 0.0f;
    float m_maxVal = 1.0f;

    std::mutex m_dataMutex;
};

} // namespace gridlock::ui
