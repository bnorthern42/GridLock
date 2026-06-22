#include "DomainHeatmapRenderer.hpp"
#include <QVulkanFunctions>

namespace gridlock::ui {

const uint32_t heatmap_vert_spv[] = 
#include "shaders/heatmap.vert.inc"
;
const uint32_t heatmap_frag_spv[] = 
#include "shaders/heatmap.frag.inc"
;

DomainHeatmapRenderer::DomainHeatmapRenderer(QVulkanWindow *w) : m_window(w) {
}

uint32_t DomainHeatmapRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

void DomainHeatmapRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkDevice dev = m_window->device();
    QVulkanDeviceFunctions *devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    devFuncs->vkCreateBuffer(dev, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements memRequirements;
    devFuncs->vkGetBufferMemoryRequirements(dev, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    devFuncs->vkAllocateMemory(dev, &allocInfo, nullptr, &bufferMemory);
    devFuncs->vkBindBufferMemory(dev, buffer, bufferMemory, 0);
}

void DomainHeatmapRenderer::initResources() {
    VkDevice dev = m_window->device();
    QVulkanDeviceFunctions *devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 1024;
    imageInfo.extent.height = 1024;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    devFuncs->vkCreateImage(dev, &imageInfo, nullptr, &m_image);

    VkMemoryRequirements memRequirements;
    devFuncs->vkGetImageMemoryRequirements(dev, m_image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    devFuncs->vkAllocateMemory(dev, &allocInfo, nullptr, &m_imageMemory);
    devFuncs->vkBindImageMemory(dev, m_image, m_imageMemory, 0);
}

void DomainHeatmapRenderer::initSwapChainResources() {
}

void DomainHeatmapRenderer::releaseSwapChainResources() {
}

void DomainHeatmapRenderer::releaseResources() {
}

void DomainHeatmapRenderer::uploadData(const std::vector<double>& matrix, int rows, int cols) {
    // 1. Convert double matrix to floats
    m_pendingUpload.resize(matrix.size());
    for(size_t i = 0; i < matrix.size(); ++i) {
        m_pendingUpload[i] = static_cast<float>(matrix[i]);
    }
    VkDevice dev = m_window->device();
    QVulkanDeviceFunctions *devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);
    
    VkDeviceSize bufferSize = m_pendingUpload.size() * sizeof(float);
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    devFuncs->vkMapMemory(dev, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_pendingUpload.data(), (size_t) bufferSize);
    devFuncs->vkUnmapMemory(dev, stagingBufferMemory);

    // Normally we would use a command buffer to transition layout, copy buffer to image,
    // and transition to shader read here.
    // devFuncs->vkCmdCopyBufferToImage(...)

    devFuncs->vkDestroyBuffer(dev, stagingBuffer, nullptr);
    devFuncs->vkFreeMemory(dev, stagingBufferMemory, nullptr);

    m_needsUpload = true;
    m_window->requestUpdate();
}

void DomainHeatmapRenderer::startNextFrame() {
    VkDevice dev = m_window->device();
    QVulkanDeviceFunctions *devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);
    VkCommandBuffer cb = m_window->currentCommandBuffer();

    if (m_needsUpload && m_pendingWidth > 0 && m_pendingHeight > 0) {
        // Here we would normally allocate staging buffer, map, copy m_pendingUpload, unmap,
        // and copy buffer to image.
        // For the scope of Phase 1 to prove compilation against RHI/Vulkan, we simulate this.
        m_needsUpload = false;
    }

    VkClearColorValue clearColor = {{0.1f, 0.1f, 0.15f, 1.0f}};
    VkClearDepthStencilValue clearDS = {1.0f, 0};
    VkClearValue clearValues[2];
    memset(clearValues, 0, sizeof(clearValues));
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    VkRenderPassBeginInfo rpBeginInfo;
    memset(&rpBeginInfo, 0, sizeof(rpBeginInfo));
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = m_window->defaultRenderPass();
    rpBeginInfo.framebuffer = m_window->currentFramebuffer();
    const QSize sz = m_window->swapChainImageSize();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = 2;
    rpBeginInfo.pClearValues = clearValues;

    devFuncs->vkCmdBeginRenderPass(cb, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Shader rendering logic
    // devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    // devFuncs->vkCmdDraw(cb, 3, 1, 0, 0); // screen-space quad/triangle

    devFuncs->vkCmdEndRenderPass(cb);

    m_window->frameReady();
    m_window->requestUpdate(); 
}

} // namespace gridlock::ui
