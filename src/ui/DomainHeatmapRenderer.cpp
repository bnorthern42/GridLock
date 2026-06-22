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
    imageInfo.extent.width = 1;
    imageInfo.extent.height = 1;
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

    // Create 1x1 default texture
    std::vector<float> defaultTex = {0.1f};
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    devFuncs->vkMapMemory(dev, stagingBufferMemory, 0, sizeof(float), 0, &data);
    memcpy(data, defaultTex.data(), sizeof(float));
    devFuncs->vkUnmapMemory(dev, stagingBufferMemory);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_window->graphicsQueueFamilyIndex();
    VkCommandPool commandPool;
    devFuncs->vkCreateCommandPool(dev, &poolInfo, nullptr, &commandPool);

    VkCommandBufferAllocateInfo allocInfoCB{};
    allocInfoCB.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfoCB.commandPool = commandPool;
    allocInfoCB.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfoCB.commandBufferCount = 1;
    VkCommandBuffer cb;
    devFuncs->vkAllocateCommandBuffers(dev, &allocInfoCB, &cb);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    devFuncs->vkBeginCommandBuffer(cb, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    devFuncs->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {1, 1, 1};
    devFuncs->vkCmdCopyBufferToImage(cb, stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    devFuncs->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    devFuncs->vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    VkQueue graphicsQueue = m_window->graphicsQueue();
    devFuncs->vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    devFuncs->vkQueueWaitIdle(graphicsQueue);

    devFuncs->vkFreeCommandBuffers(dev, commandPool, 1, &cb);
    devFuncs->vkDestroyCommandPool(dev, commandPool, nullptr);
    devFuncs->vkDestroyBuffer(dev, stagingBuffer, nullptr);
    devFuncs->vkFreeMemory(dev, stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    devFuncs->vkCreateImageView(dev, &viewInfo, nullptr, &m_imageView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    devFuncs->vkCreateSampler(dev, &samplerInfo, nullptr, &m_sampler);

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    devFuncs->vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descriptorSetLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfoDesc{};
    poolInfoDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfoDesc.poolSizeCount = 1;
    poolInfoDesc.pPoolSizes = &poolSize;
    poolInfoDesc.maxSets = 1;
    devFuncs->vkCreateDescriptorPool(dev, &poolInfoDesc, nullptr, &m_descriptorPool);

    VkDescriptorSetAllocateInfo allocInfoDesc{};
    allocInfoDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoDesc.descriptorPool = m_descriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &m_descriptorSetLayout;
    devFuncs->vkAllocateDescriptorSets(dev, &allocInfoDesc, &m_descriptorSet);

    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = m_imageView;
    descImageInfo.sampler = m_sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    devFuncs->vkUpdateDescriptorSets(dev, 1, &descriptorWrite, 0, nullptr);
}

void DomainHeatmapRenderer::initSwapChainResources() {
}

void DomainHeatmapRenderer::releaseSwapChainResources() {
}

void DomainHeatmapRenderer::releaseResources() {
}

void DomainHeatmapRenderer::uploadData(const std::vector<double>& matrix, int rows, int cols, float min_val, float max_val) {
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

    m_minVal = min_val;
    m_maxVal = max_val;

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
    
    // Push constants
    struct {
        float min_val;
        float max_val;
    } pc;
    pc.min_val = m_minVal;
    pc.max_val = m_maxVal;
    
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        devFuncs->vkCmdPushConstants(cb, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    }
    
    // devFuncs->vkCmdDraw(cb, 3, 1, 0, 0); // screen-space quad/triangle

    devFuncs->vkCmdEndRenderPass(cb);

    m_window->frameReady();
}

} // namespace gridlock::ui
