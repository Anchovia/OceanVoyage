#include "VulkanContext.h"
#include "VulkanContext_Private.h"
#include "renderer/Types.h"
#include "platform/Window.h"
#include "game/Camera.h"

#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <chrono>
#include <cmath>
#include <utility>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
//  Constructor / Destructor
// ============================================================
VulkanContext::VulkanContext(Window& window) : m_window(window) {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createPostRenderPass();
    createSmaaRenderPass();
    createTaaRenderPass();
    createDescriptorSetLayout();
    createOceanDescriptorSetLayout();
    createScenePipelineLayout();
    createSkyPipeline();
    createUIPipeline();
    createOceanPipeline();
    createShipPipeline();
    createPostPipeline();
    createSmaaPipelines();
    createTaaPipeline();
    createDepthResources();
    createOffscreenResources();
    createPlanarReflectionResources();
    createSmaaResources();
    createShadowResources();
    createShadowPipeline();
    createPortPipeline();
    createFramebuffers();
    createCommandPool();
    createSmaaLookupTextures();
    createTaaResources(); // needs the command pool for the initial layout transition
#ifdef PASTEL_DEV_BUILD
    createDevTools();
#endif
    createUIBuffer();
    loadImportedShipMesh();
    createPortMesh();
    createOceanMesh();
    createOceanNormalTextures();
    createOceanFFT();
    createUniformBuffers();
    createReflectionUniformBuffers();
    createShadowSampler();
    createPostSampler();
    createDescriptorPool();
    createDescriptorSets();
    createReflectionDescriptorSets();
    createOceanDescriptors();
    createPostDescriptors();
    createSmaaDescriptors();
    createTaaDescriptors();
    createCommandBuffers();
    createSyncObjects();
}

VulkanContext::~VulkanContext() {
    waitIdle();

#ifdef PASTEL_DEV_BUILD
    destroyDevTools();
#endif

    // Free all GpuBuffers here (device still alive). Their dtors run again after this
    // body when members destruct, but destroy() is idempotent so those are no-ops.
    m_deletionQueue.clear();

    cleanupSwapchain();

    m_uniformBuffers.clear();
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorPool(m_device, m_oceanDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_oceanDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    m_reflectionUniformBuffers.clear();
    destroyOceanFFT();
    m_oceanNormalB.destroy();
    m_oceanNormalA.destroy();
    m_shipSpecularTex.destroy();
    m_shipNormalTex.destroy();
    m_shipAlbedoTex.destroy();
    m_shipMesh.vbuf.destroy();
    m_portMesh.vbuf.destroy();
    m_oceanIndexBuffer.destroy();
    m_oceanVertexBuffer.destroy();
    m_uiBuffer.clear();
    vkDestroyPipeline(m_device, m_uiPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_uiPipelineLayout, nullptr);
    vkDestroyPipeline(m_device, m_shipPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_shipPipelineLayout, nullptr);
    vkDestroyPipeline(m_device, m_portShadowPipeline, nullptr);
    vkDestroyPipeline(m_device, m_portPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_portPipelineLayout, nullptr);
    vkDestroyPipeline(m_device, m_oceanPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_oceanPipelineLayout, nullptr);
    vkDestroyPipeline(m_device, m_skyPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyPipeline      (m_device, m_shadowPipeline,       nullptr);
    vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr);
    vkDestroySampler       (m_device, m_shadowSampler,        nullptr);
    for (uint32_t c = 0; c < CSM_CASCADES; c++) {
        vkDestroyFramebuffer(m_device, m_shadowFramebuffers[c], nullptr);
        vkDestroyImageView  (m_device, m_shadowLayerView[c],    nullptr);
    }
    vkDestroyRenderPass    (m_device, m_shadowRenderPass,     nullptr);
    vkDestroyImageView  (m_device, m_shadowImageView,    nullptr);
    vkDestroyImage      (m_device, m_shadowImage,        nullptr);
    vkFreeMemory        (m_device, m_shadowImageMemory,  nullptr);
    vkDestroyPipeline           (m_device, m_postLdrPipeline,         nullptr);
    vkDestroyPipeline           (m_device, m_postPipeline,            nullptr);
    vkDestroyPipelineLayout     (m_device, m_postPipelineLayout,      nullptr);
    vkDestroyDescriptorPool     (m_device, m_postDescriptorPool,      nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_postDescriptorSetLayout, nullptr);
    vkDestroyPipeline           (m_device, m_taaPipeline,            nullptr);
    vkDestroyPipelineLayout     (m_device, m_taaPipelineLayout,      nullptr);
    vkDestroyDescriptorPool     (m_device, m_taaDescriptorPool,      nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_taaDescriptorSetLayout, nullptr);
    vkDestroyPipeline           (m_device, m_smaaNeighborhoodPipeline,       nullptr);
    vkDestroyPipelineLayout     (m_device, m_smaaNeighborhoodPipelineLayout, nullptr);
    vkDestroyPipeline           (m_device, m_smaaBlendPipeline,              nullptr);
    vkDestroyPipelineLayout     (m_device, m_smaaBlendPipelineLayout,        nullptr);
    vkDestroyPipeline           (m_device, m_smaaEdgePipeline,               nullptr);
    vkDestroyPipelineLayout     (m_device, m_smaaEdgePipelineLayout,         nullptr);
    vkDestroyDescriptorPool     (m_device, m_smaaDescriptorPool,             nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_smaaNeighborhoodDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_smaaBlendDescriptorSetLayout,        nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_smaaEdgeDescriptorSetLayout,         nullptr);
    m_smaaAreaTex.destroy();
    m_smaaSearchTex.destroy();
    vkDestroySampler            (m_device, m_sceneDepthSampler,       nullptr);
    vkDestroySampler            (m_device, m_postSampler,             nullptr);
    vkDestroyRenderPass         (m_device, m_postLdrRenderPass,       nullptr);
    vkDestroyRenderPass         (m_device, m_taaRenderPass,           nullptr);
    vkDestroyRenderPass         (m_device, m_smaaRenderPass,          nullptr);
    vkDestroyRenderPass         (m_device, m_postRenderPass,          nullptr);
    vkDestroyRenderPass         (m_device, m_sceneLoadRenderPass,     nullptr);
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device, m_imageAvailable[i], nullptr);
        vkDestroyFence(m_device, m_inFlight[i], nullptr);
    }
    for (auto sem : m_renderFinished)
        vkDestroySemaphore(m_device, sem, nullptr);
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyDevice(m_device, nullptr);
    if (kEnableValidation) DestroyDebugMessenger(m_instance, m_debugMessenger);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

void VulkanContext::waitIdle() { vkDeviceWaitIdle(m_device); }

void VulkanContext::deferDestroy(GpuBuffer&& buf) {
    if (buf.buffer != VK_NULL_HANDLE)
        m_deletionQueue.push_back({std::move(buf), m_frameCount});
}

// ============================================================
//  Shader helpers
// ============================================================
std::vector<char> VulkanContext::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Cannot open file: " + path);
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

VkShaderModule VulkanContext::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(m_device, &info, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module");
    return mod;
}

// ============================================================
//  Memory / buffer helpers
// ============================================================
uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

GpuBuffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties)
{
    GpuBuffer buf;
    buf.device = m_device;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = size;
    bufInfo.usage       = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufInfo, nullptr, &buf.buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, buf.buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &buf.memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    vkCheck(vkBindBufferMemory(m_device, buf.buffer, buf.memory, 0),
        "Failed to bind buffer memory");
    return buf;
}

VkFormat VulkanContext::findSupportedFormat(const std::vector<VkFormat>& candidates,
    VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features)
            return format;
    }
    throw std::runtime_error("Failed to find supported format");
}

VkFormat VulkanContext::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat VulkanContext::findSceneColorFormat() {
    return findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
}

void VulkanContext::createImage(uint32_t width, uint32_t height, VkFormat format,
    VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
    VkImage& image, VkDeviceMemory& memory, uint32_t mipLevels, uint32_t arrayLayers)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent        = {width, height, 1};
    imageInfo.mipLevels     = mipLevels;
    imageInfo.arrayLayers   = arrayLayers;
    imageInfo.format        = format;
    imageInfo.tiling        = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = usage;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, image, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate image memory");
    vkCheck(vkBindImageMemory(m_device, image, memory, 0),
        "Failed to bind image memory");
}

// ============================================================
//  Swapchain cleanup / recreation
// ============================================================
void VulkanContext::cleanupSwapchain() {
    for (auto fb : m_sceneFramebuffers)    vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto fb : m_sceneLoadFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto fb : m_reflectionFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto fb : m_postFramebuffers)     vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto fb : m_smaaEdgeFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto fb : m_smaaBlendFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto fb : m_taaFramebuffers)      vkDestroyFramebuffer(m_device, fb, nullptr);
    for (size_t i = 0; i < m_taaImage.size(); i++) {
        vkDestroyImageView(m_device, m_taaView[i],   nullptr);
        vkDestroyImage    (m_device, m_taaImage[i],  nullptr);
        vkFreeMemory      (m_device, m_taaMemory[i], nullptr);
    }

    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkDestroyImage    (m_device, m_depthImage,     nullptr);
    vkFreeMemory      (m_device, m_depthImageMemory, nullptr);
    for (size_t i = 0; i < m_sceneDepthCopyImage.size(); i++) {
        vkDestroyImageView(m_device, m_sceneDepthCopyView[i],   nullptr);
        vkDestroyImage    (m_device, m_sceneDepthCopyImage[i],  nullptr);
        vkFreeMemory      (m_device, m_sceneDepthCopyMemory[i], nullptr);
    }
    for (size_t i = 0; i < m_offscreenImage.size(); i++) {
        vkDestroyImageView(m_device, m_offscreenView[i],   nullptr);
        vkDestroyImage    (m_device, m_offscreenImage[i],  nullptr);
        vkFreeMemory      (m_device, m_offscreenMemory[i], nullptr);
    }
    for (size_t i = 0; i < m_sceneColorCopyImage.size(); i++) {
        vkDestroyImageView(m_device, m_sceneColorCopyView[i],   nullptr);
        vkDestroyImage    (m_device, m_sceneColorCopyImage[i],  nullptr);
        vkFreeMemory      (m_device, m_sceneColorCopyMemory[i], nullptr);
    }
    for (size_t i = 0; i < m_reflectionImage.size(); i++) {
        vkDestroyImageView(m_device, m_reflectionView[i],   nullptr);
        vkDestroyImage    (m_device, m_reflectionImage[i],  nullptr);
        vkFreeMemory      (m_device, m_reflectionMemory[i], nullptr);
    }
    for (size_t i = 0; i < m_smaaEdgeImage.size(); i++) {
        vkDestroyImageView(m_device, m_smaaEdgeView[i],   nullptr);
        vkDestroyImage    (m_device, m_smaaEdgeImage[i],  nullptr);
        vkFreeMemory      (m_device, m_smaaEdgeMemory[i], nullptr);
    }
    for (size_t i = 0; i < m_smaaBlendImage.size(); i++) {
        vkDestroyImageView(m_device, m_smaaBlendView[i],   nullptr);
        vkDestroyImage    (m_device, m_smaaBlendImage[i],  nullptr);
        vkFreeMemory      (m_device, m_smaaBlendMemory[i], nullptr);
    }
    for (auto fb : m_ldrFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (size_t i = 0; i < m_ldrImage.size(); i++) {
        vkDestroyImageView(m_device, m_ldrView[i],   nullptr);
        vkDestroyImage    (m_device, m_ldrImage[i],  nullptr);
        vkFreeMemory      (m_device, m_ldrMemory[i], nullptr);
    }
    for (auto iv : m_swapchainImageViews)  vkDestroyImageView  (m_device, iv, nullptr);
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
}

void VulkanContext::recreateSwapchain() {
    // Pause while minimized
    int w = 0, h = 0;
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(m_window.handle(), &w, &h);
        glfwWaitEvents();
    }
    waitIdle();
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createDepthResources();
    createOffscreenResources();
    createPlanarReflectionResources();
    createSmaaResources();
    createTaaResources();
    createFramebuffers();
    updatePostDescriptors();   // offscreen views were recreated
    updateOceanDescriptors();  // planar reflection views were recreated
    updateSmaaDescriptors();   // SMAA intermediate views were recreated
    updateTaaDescriptors();    // TAA history/depth views were recreated

    // Swapchain image count may have changed — recreate per-image present semaphores
    for (auto& sem : m_renderFinished) {
        vkDestroySemaphore(m_device, sem, nullptr);
        sem = VK_NULL_HANDLE;
    }
    m_renderFinished.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& sem : m_renderFinished)
        vkCheck(vkCreateSemaphore(m_device, &semInfo, nullptr, &sem),
            "Failed to create present semaphore");
    m_imagesInFlight.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
}

// ============================================================
//  Buffer copy (one-shot command buffer)
// ============================================================
void VulkanContext::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer),
        "Failed to allocate copy command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo),
        "Failed to begin copy command buffer");

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkCheck(vkEndCommandBuffer(commandBuffer),
        "Failed to end copy command buffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &fence),
        "Failed to create copy fence");

    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence),
        "Failed to submit copy command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for copy fence");

    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

// ============================================================
//  Image layout transition / buffer→image copy (one-shot command buffers)
// ============================================================
// NOTE: these share the one-shot submit boilerplate with copyBuffer above. Once a
// third use appears, extracting begin/endSingleTimeCommands() is worth doing.
void VulkanContext::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd),
        "Failed to allocate layout transition command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &beginInfo),
        "Failed to begin layout transition command buffer");

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Fresh attachment bound as sampled history before its first write
        // (TAA): contents are garbage but the layout must be legal to bind.
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("Unsupported image layout transition");
    }
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end layout transition command buffer");

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &fence),
        "Failed to create layout transition fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submit, fence),
        "Failed to submit layout transition command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for layout transition fence");
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

void VulkanContext::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd),
        "Failed to allocate buffer-to-image command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &beginInfo),
        "Failed to begin buffer-to-image command buffer");

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end buffer-to-image command buffer");

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &fence),
        "Failed to create buffer-to-image fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submit, fence),
        "Failed to submit buffer-to-image command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for buffer-to-image fence");
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

// Builds the mip chain by successive half-resolution linear blits (standard vkCmdBlitImage
// pattern). Precondition: mip 0 holds the source data and is in TRANSFER_DST_OPTIMAL; mips
// 1..N-1 are UNDEFINED. On return every level (all array layers) is SHADER_READ_ONLY_OPTIMAL.
void VulkanContext::generateMipmaps(VkImage image, VkFormat format, int32_t width, int32_t height,
    uint32_t mipLevels, uint32_t layerCount)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        throw std::runtime_error("Texture format does not support linear blit for mipmap generation");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd),
        "Failed to allocate mipmap command buffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &begin),
        "Failed to begin mipmap command buffer");

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image               = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = layerCount;
    barrier.subresourceRange.levelCount     = 1;

    int32_t mipW = width, mipH = height;
    for (uint32_t i = 1; i < mipLevels; i++) {
        // Source level (i-1): TRANSFER_DST -> TRANSFER_SRC
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Destination level (i): UNDEFINED -> TRANSFER_DST
        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipW, mipH, 1};
        blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel       = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount     = layerCount;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1};
        blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel       = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount     = layerCount;
        vkCmdBlitImage(cmd,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // Source level (i-1) is done: TRANSFER_SRC -> SHADER_READ
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        if (mipW > 1) mipW /= 2;
        if (mipH > 1) mipH /= 2;
    }

    // Last level is still TRANSFER_DST -> SHADER_READ
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end mipmap command buffer");
    VkSubmitInfo mipSubmit{};
    mipSubmit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    mipSubmit.commandBufferCount = 1;
    mipSubmit.pCommandBuffers    = &cmd;
    VkFenceCreateInfo mipFenceInfo{};
    mipFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence mipFence;
    vkCheck(vkCreateFence(m_device, &mipFenceInfo, nullptr, &mipFence),
        "Failed to create mipmap fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &mipSubmit, mipFence),
        "Failed to submit mipmap command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &mipFence, VK_TRUE, UINT64_MAX),
        "Failed to wait for mipmap fence");
    vkDestroyFence(m_device, mipFence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}
