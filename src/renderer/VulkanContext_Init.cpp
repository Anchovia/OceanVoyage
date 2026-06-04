#include "VulkanContext.h"
#include "VulkanContext_Private.h"
#include "renderer/Types.h"
#include "platform/Window.h"
#include "world/World.h"
#include "game/Camera.h"

#include "AreaTex.h"
#include "SearchTex.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef PASTEL_DEV_BUILD
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#endif

namespace {
struct LoadedImageRGBA8 {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

LoadedImageRGBA8 loadImageRGBA8(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        std::string message = "Failed to load texture image: " + path;
        if (reason) {
            message += " (";
            message += reason;
            message += ")";
        }
        if (data) stbi_image_free(data);
        throw std::runtime_error(message);
    }

    const size_t size = (size_t)width * (size_t)height * 4;
    LoadedImageRGBA8 image;
    image.width = width;
    image.height = height;
    image.pixels.assign(data, data + size);
    stbi_image_free(data);
    return image;
}

bool fileExists(const std::string& path) {
    return std::ifstream(path, std::ios::binary).good();
}
}

// ============================================================
//  Instance
// ============================================================
void VulkanContext::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "GameEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "CustomEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    uint32_t    glfwExtCount = 0;
    const char** glfwExts    = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
    if (kEnableValidation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // Fill debug create-info so it also covers instance creation/destruction
    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    debugInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugCallback;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (kEnableValidation) {
        createInfo.enabledLayerCount   = (uint32_t)kValidationLayers.size();
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
        createInfo.pNext               = &debugInfo;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan instance");
}

// ============================================================
//  Debug messenger
// ============================================================
void VulkanContext::setupDebugMessenger() {
    if (!kEnableValidation) return;
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    CreateDebugMessenger(m_instance, &info, &m_debugMessenger);
}

// ============================================================
//  Surface
// ============================================================
void VulkanContext::createSurface() {
    if (glfwCreateWindowSurface(m_instance, m_window.handle(), nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");
}

// ============================================================
//  Physical device
// ============================================================
VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphics = i;
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) indices.present = i;
        if (indices.complete()) break;
    }
    return indices;
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Prefer discrete GPU; otherwise take first suitable device
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (auto device : devices) {
        if (!findQueueFamilies(device).complete()) continue;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = device;
            std::cout << "GPU (discrete): " << props.deviceName << "\n";
            return;
        }
        if (fallback == VK_NULL_HANDLE) fallback = device;
    }
    if (fallback != VK_NULL_HANDLE) {
        m_physicalDevice = fallback;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(fallback, &props);
        std::cout << "GPU (integrated): " << props.deviceName << "\n";
        return;
    }
    throw std::runtime_error("No suitable GPU found");
}

// ============================================================
//  Logical device + queues
// ============================================================
void VulkanContext::createLogicalDevice() {
    auto indices = findQueueFamilies(m_physicalDevice);
    std::set<uint32_t> uniqueFamilies = { *indices.graphics, *indices.present };

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supported);
    VkPhysicalDeviceFeatures features{};
    if (supported.samplerAnisotropy) {
        features.samplerAnisotropy = VK_TRUE;
        m_anisotropyEnabled = true;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        m_maxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = (uint32_t)queueInfos.size();
    createInfo.pQueueCreateInfos       = queueInfos.data();
    createInfo.pEnabledFeatures        = &features;
    createInfo.enabledExtensionCount   = (uint32_t)kDeviceExtensions.size();
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device");

    vkGetDeviceQueue(m_device, *indices.graphics, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, *indices.present,  0, &m_presentQueue);
}

// ============================================================
//  Swapchain
// ============================================================
void VulkanContext::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, modes.data());

    // Format: prefer BGRA8 sRGB
    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            chosenFormat = f;

    // Present mode: FIFO for VSync, otherwise prefer mailbox (low latency) and fall back to FIFO.
    VkPresentModeKHR chosenMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_vsyncEnabled) {
        for (auto& m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) { chosenMode = m; break; }
    }

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent = { (uint32_t)m_window.width(), (uint32_t)m_window.height() };
        extent.width  = std::clamp(extent.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{};
    info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface          = m_surface;
    info.minImageCount    = imageCount;
    info.imageFormat      = chosenFormat.format;
    info.imageColorSpace  = chosenFormat.colorSpace;
    info.imageExtent      = extent;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform     = caps.currentTransform;
    info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode      = chosenMode;
    info.clipped          = VK_TRUE;

    auto indices = findQueueFamilies(m_physicalDevice);
    uint32_t queueFamilies[] = { *indices.graphics, *indices.present };
    if (*indices.graphics != *indices.present) {
        info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices   = queueFamilies;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain");

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainFormat = chosenFormat.format;
    m_swapchainExtent = extent;
}

// ============================================================
//  Image views
// ============================================================
void VulkanContext::createImageViews() {
    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo info{};
        info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                           = m_swapchainImages[i];
        info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        info.format                          = m_swapchainFormat;
        info.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 VK_COMPONENT_SWIZZLE_IDENTITY };
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(m_device, &info, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image view");
    }
}

// ============================================================
//  Render pass
// ============================================================
void VulkanContext::createRenderPass() {
    m_sceneColorFormat = findSceneColorFormat();

    VkAttachmentDescription color{};
    color.format         = m_sceneColorFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // sampled by the post pass

    VkAttachmentDescription depth{};
    depth.format         = findDepthFormat();
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Offscreen color write must finish before the post pass samples it
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkAttachmentDescription attachments[] = {color, depth};
    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments    = attachments;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 2;
    info.pDependencies   = deps;

    if (vkCreateRenderPass(m_device, &info, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");
}

// ============================================================
//  Graphics pipelines
// ============================================================
// Shared graphics-pipeline builder. Fills all common
// fixed-function state; per-pipeline differences come from PipelineConfig.
VkPipeline VulkanContext::createPipeline(const PipelineConfig& cfg) {
    auto vertCode = readFile(cfg.vertPath);
    auto fragCode = readFile(cfg.fragPath);
    VkShaderModule vertMod = createShaderModule(vertCode);
    VkShaderModule fragMod = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = (uint32_t)cfg.bindings.size();
    vertexInput.pVertexBindingDescriptions      = cfg.bindings.data();
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)cfg.attributes.size();
    vertexInput.pVertexAttributeDescriptions    = cfg.attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0, 0,
        (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, m_swapchainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    // Viewport/scissor as dynamic state so window resize doesn't need a pipeline rebuild
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = cfg.cullMode;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (cfg.alphaBlend) {
        blendAttach.blendEnable         = VK_TRUE;
        blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
        blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttach;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = cfg.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = cfg.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp   = cfg.depthTest ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState   = &msaa;
    pipelineInfo.pColorBlendState    = &blend;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = cfg.layout;
    pipelineInfo.renderPass          = (cfg.renderPass != VK_NULL_HANDLE) ? cfg.renderPass : m_renderPass;
    pipelineInfo.subpass             = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create graphics pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
    return pipeline;
}

void VulkanContext::createGraphicsPipeline() {
    // Player / selector pipeline layout: UBO + shadow sampler descriptor
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_descriptorSetLayout;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");

    PipelineConfig cfg;
    cfg.vertPath   = "shaders/triangle.vert.spv";
    cfg.fragPath   = "shaders/triangle.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(Vertex),       VK_VERTEX_INPUT_RATE_VERTEX   },
        { 1, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)             },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)          },
        { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, pos)       },
        { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, topColor)  },
        { 4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, sideColor) },
    };
    cfg.cullMode   = VK_CULL_MODE_BACK_BIT;
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_pipelineLayout;
    m_pipeline = createPipeline(cfg);
}

void VulkanContext::createChunkPipeline() {
    PipelineConfig cfg;
    cfg.vertPath   = "shaders/chunk.vert.spv";
    cfg.fragPath   = "shaders/chunk.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, pos)    },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal) },
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, color)  },
        { 3, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ChunkVertex, uv)     },
        { 4, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ChunkVertex, layer)  },
    };
    cfg.cullMode   = VK_CULL_MODE_BACK_BIT;
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_pipelineLayout;
    m_chunkPipeline = createPipeline(cfg);
}

void VulkanContext::createUIPipeline() {
    // No descriptor sets — UI vertices are already in NDC
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_uiPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create UI pipeline layout");

    PipelineConfig cfg;
    cfg.vertPath   = "shaders/ui.vert.spv";
    cfg.fragPath   = "shaders/ui.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, pos)   },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, color) },
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;
    cfg.depthTest  = false;   // UI draws on top — no depth test/write
    cfg.alphaBlend = true;    // semi-transparent panels
    cfg.layout     = m_uiPipelineLayout;
    cfg.renderPass = m_postRenderPass;
    m_uiPipeline = createPipeline(cfg);
}

void VulkanContext::createObjectPipeline() {
    PipelineConfig cfg;
    cfg.vertPath   = "shaders/object.vert.spv";
    cfg.fragPath   = "shaders/chunk.frag.spv";  // reuse chunk fragment shader
    cfg.bindings   = {
        { 0, sizeof(ChunkVertex),    VK_VERTEX_INPUT_RATE_VERTEX   },
        { 1, sizeof(ObjectInstance), VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, pos)      },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal)   },
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, color)    },
        { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ObjectInstance, pos)   },
        { 4, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, scale) },
        { 5, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, rot)   },
        { 6, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ChunkVertex, uv)       },
        { 7, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ChunkVertex, layer)    },
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;  // procedural mesh — winding not guaranteed outward
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_pipelineLayout;   // reuse UBO descriptor layout
    m_objectPipeline = createPipeline(cfg);
}

void VulkanContext::createOceanDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[5]{};

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    // FFT displacement map — sampled by the ocean vertex shader to displace the mesh.
    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 5;
    info.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_oceanDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean descriptor set layout");
}

void VulkanContext::createOceanPipeline() {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_oceanDescriptorSetLayout;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_oceanPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean pipeline layout");

    PipelineConfig cfg;
    cfg.vertPath   = "shaders/ocean.vert.spv";
    cfg.fragPath   = "shaders/ocean.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, // grid position only
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;  // two-sided: visible from under a wave crest
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_oceanPipelineLayout;
    m_oceanPipeline = createPipeline(cfg);
}

void VulkanContext::createOceanMesh() {
    // Camera-centered concentric ocean mesh: dense near the ship/camera, progressively
    // larger triangles toward the horizon. This keeps silhouette quality close by while
    // covering the long, grazing-angle view required by the sailing camera.
    constexpr int   SECTORS = 512;
    constexpr float TWO_PI  = 6.28318530718f;

    std::vector<float> radii;
    auto appendRings = [&radii](float endRadius, float step) {
        float r = radii.empty() ? step : radii.back() + step;
        for (; r <= endRadius + 0.001f; r += step)
            radii.push_back(r);
    };

    appendRings(  48.0f,  0.5f);
    appendRings( 128.0f,  1.0f);
    appendRings( 384.0f,  4.0f);
    appendRings(1024.0f,  8.0f);
    appendRings(3072.0f, 24.0f);

    std::vector<glm::vec3> verts;
    verts.reserve(1 + radii.size() * SECTORS);
    verts.push_back({ 0.0f, 0.0f, 0.0f });

    for (float r : radii) {
        for (int s = 0; s < SECTORS; s++) {
            const float a = TWO_PI * (float)s / (float)SECTORS;
            verts.push_back({ std::cos(a) * r, std::sin(a) * r, 0.0f });
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(SECTORS * 3 + (radii.size() - 1) * SECTORS * 6);

    const uint32_t firstRing = 1;
    for (int s = 0; s < SECTORS; s++) {
        uint32_t b = firstRing + (uint32_t)s;
        uint32_t c = firstRing + (uint32_t)((s + 1) % SECTORS);
        indices.insert(indices.end(), { 0, b, c });
    }

    for (size_t ring = 1; ring < radii.size(); ring++) {
        uint32_t prevBase = 1 + (uint32_t)(ring - 1) * SECTORS;
        uint32_t currBase = 1 + (uint32_t)ring * SECTORS;
        for (int s = 0; s < SECTORS; s++) {
            uint32_t prev0 = prevBase + (uint32_t)s;
            uint32_t prev1 = prevBase + (uint32_t)((s + 1) % SECTORS);
            uint32_t curr0 = currBase + (uint32_t)s;
            uint32_t curr1 = currBase + (uint32_t)((s + 1) % SECTORS);
            indices.insert(indices.end(), { prev0, curr0, prev1,  prev1, curr0, curr1 });
        }
    }
    m_oceanIndexCount = (uint32_t)indices.size();

    VkDeviceSize vSize = sizeof(glm::vec3) * verts.size();
    m_oceanVertexBuffer = createBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* vMapped;
    vkMapMemory(m_device, m_oceanVertexBuffer.memory, 0, vSize, 0, &vMapped);
    memcpy(vMapped, verts.data(), vSize);
    vkUnmapMemory(m_device, m_oceanVertexBuffer.memory);

    VkDeviceSize iSize = sizeof(uint32_t) * indices.size();
    m_oceanIndexBuffer = createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* iMapped;
    vkMapMemory(m_device, m_oceanIndexBuffer.memory, 0, iSize, 0, &iMapped);
    memcpy(iMapped, indices.data(), iSize);
    vkUnmapMemory(m_device, m_oceanIndexBuffer.memory);
}

void VulkanContext::createOceanNormalTextures() {
    struct Wave {
        float fx, fy;
        float amp;
        float phase;
    };

    auto makeNormalMap = [](uint32_t size, const std::vector<Wave>& waves, float strength) {
        std::vector<uint8_t> pixels((size_t)size * (size_t)size * 4);
        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                const float u = (float)x / (float)size;
                const float v = (float)y / (float)size;
                glm::vec2 slope(0.0f);

                for (const Wave& w : waves) {
                    const float phase = 6.2831853f * (w.fx * u + w.fy * v) + w.phase;
                    const float c = std::cos(phase);
                    slope.x += w.amp * w.fx * c;
                    slope.y += w.amp * w.fy * c;
                }

                slope *= strength;
                glm::vec3 n = glm::normalize(glm::vec3(-slope.x, -slope.y, 1.0f));
                uint8_t* p = &pixels[((size_t)y * size + x) * 4];
                p[0] = (uint8_t)(std::clamp(n.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                p[1] = (uint8_t)(std::clamp(n.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                p[2] = (uint8_t)(std::clamp(n.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                p[3] = 255;
            }
        }
        return pixels;
    };

    auto uploadTiledNormalMap = [this](uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels) {
        TextureResource tex = createTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM,
            pixels.data(), (VkDeviceSize)pixels.size(), /*withSampler=*/false, /*mipmapped=*/true);

        const uint32_t mipLevels = (uint32_t)std::floor(std::log2((float)std::max(width, height))) + 1u;
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.minLod       = 0.0f;
        si.maxLod       = (float)mipLevels;
        si.anisotropyEnable = m_anisotropyEnabled ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy    = m_maxAnisotropy;
        if (vkCreateSampler(m_device, &si, nullptr, &tex.sampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create ocean normal sampler");

        return tex;
    };

    // Tileable multi-frequency normal maps. They are generated once, then sampled like
    // ordinary authored ocean normals with mipmaps and anisotropic filtering.
    static const std::vector<Wave> wavesA = {
        {  6.0f,   2.0f, 0.095f, 0.20f },
        { -4.0f,   9.0f, 0.055f, 1.70f },
        { 13.0f,  -5.0f, 0.032f, 4.10f },
        { 19.0f,  11.0f, 0.018f, 2.60f },
        {-23.0f,  17.0f, 0.012f, 5.30f },
    };
    static const std::vector<Wave> wavesB = {
        { 11.0f,  -3.0f, 0.070f, 3.00f },
        { -8.0f, -13.0f, 0.046f, 0.80f },
        { 21.0f,   7.0f, 0.025f, 2.20f },
        {-29.0f,  19.0f, 0.014f, 4.70f },
        { 35.0f, -31.0f, 0.009f, 1.30f },
    };

    const uint32_t A_SIZE = 1024;
    const uint32_t B_SIZE = 512;
    std::vector<uint8_t> normalA = makeNormalMap(A_SIZE, wavesA, 0.018f);
    std::vector<uint8_t> normalB = makeNormalMap(B_SIZE, wavesB, 0.014f);

    m_oceanNormalA = uploadTiledNormalMap(A_SIZE, A_SIZE, normalA);
    m_oceanNormalB = uploadTiledNormalMap(B_SIZE, B_SIZE, normalB);
}

void VulkanContext::createShipPipeline() {
    // The hero ship needs a full orientation (bob + wave tilt + heading), so it uses a
    // model matrix supplied as a push constant rather than the yaw-only object instance.
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_shipPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ship pipeline layout");

    PipelineConfig cfg;
    cfg.vertPath   = "shaders/ship.vert.spv";
    cfg.fragPath   = "shaders/chunk.frag.spv"; // reuse chunk fragment shader
    cfg.bindings   = {
        { 0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, pos)    },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal) },
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, color)  },
        { 3, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ChunkVertex, uv)     },
        { 4, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ChunkVertex, layer)  },
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;  // procedural hull mesh — winding not guaranteed
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_shipPipelineLayout;
    m_shipPipeline = createPipeline(cfg);
}

void VulkanContext::createGrassPipeline() {
    PipelineConfig cfg;
    cfg.vertPath   = "shaders/grass.vert.spv";
    cfg.fragPath   = "shaders/grass.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(GrassCardVertex), VK_VERTEX_INPUT_RATE_VERTEX   },
        { 1, sizeof(ObjectInstance),  VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassCardVertex, pos)    },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassCardVertex, normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GrassCardVertex, uv)     },
        { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ObjectInstance, pos)     },
        { 4, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, scale)   },
        { 5, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, rot)     },
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;  // alpha cards are two-sided
    cfg.depthTest  = true;
    cfg.alphaBlend = false;              // alpha test in shader, not blended transparency
    cfg.layout     = m_pipelineLayout;   // reuse UBO + shadow + grass texture descriptor layout
    m_grassPipeline = createPipeline(cfg);
}

// ============================================================
//  UI buffer
// ============================================================
// Capacity: hotbar + inventory overlay + number/digit quads (counts, day HUD) + margin
void VulkanContext::createUIBuffer() {
    VkDeviceSize size = sizeof(UIVertex) * UI_MAX_VERTS;
    m_uiBuffer.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_uiBuffer[i] = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_uiBuffer[i].memory, 0, size, 0, &m_uiBuffer[i].mapped);
    }
}

// ============================================================
//  Tree mesh
// ============================================================
// Shared low-poly pine tree: box trunk + 3 stacked cones, flat shaded.
void VulkanContext::createObjectMeshes() {
    auto uploadMesh = [&](ObjectMesh& mesh, const std::vector<ChunkVertex>& verts) {
        mesh.count = (uint32_t)verts.size();
        VkDeviceSize size = sizeof(ChunkVertex) * verts.size();
        mesh.vbuf = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped;
        vkMapMemory(m_device, mesh.vbuf.memory, 0, size, 0, &mapped);
        memcpy(mapped, verts.data(), size);
        vkUnmapMemory(m_device, mesh.vbuf.memory);
    };
    auto uploadGrassCardMesh = [&](ObjectMesh& mesh, const std::vector<GrassCardVertex>& verts) {
        mesh.count = (uint32_t)verts.size();
        VkDeviceSize size = sizeof(GrassCardVertex) * verts.size();
        mesh.vbuf = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped;
        vkMapMemory(m_device, mesh.vbuf.memory, 0, size, 0, &mapped);
        memcpy(mapped, verts.data(), size);
        vkUnmapMemory(m_device, mesh.vbuf.memory);
    };

    // Uploads a flat-shaded mesh into the registry slot for the given object type.
    auto upload = [&](ObjectType type, const std::vector<ChunkVertex>& verts) {
        ObjectMesh& mesh = m_objectMeshes[(size_t)type];
        uploadMesh(mesh, verts);
    };
    const float LAYER_NONE  = -1.0f;
    const float LAYER_STONE = (float)tileFaceLayer(TileType::STONE, true);
    const float LAYER_WOOD  = (float)tileFaceLayer(TileType::WOOD, true);
    const float LAYER_LEAF  = (float)tileFaceLayer(TileType::LEAVES, true);
    auto makeVertex = [](glm::vec3 pos, glm::vec3 normal, glm::vec3 color, glm::vec2 uv, float layer) {
        return ChunkVertex{pos, normal, color, uv, layer};
    };
    auto pushTri = [&](std::vector<ChunkVertex>& v, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                       glm::vec3 n, glm::vec3 col, float layer) {
        v.push_back(makeVertex(a, n, col, {0.0f, 1.0f}, layer));
        v.push_back(makeVertex(b, n, col, {1.0f, 1.0f}, layer));
        v.push_back(makeVertex(c, n, col, {0.5f, 0.0f}, layer));
    };

    // ---- TREE: box trunk + 3 stacked cones (pine) ----
    {
    std::vector<ChunkVertex> verts;

    const glm::vec3 trunkColor = {0.40f, 0.26f, 0.13f};
    const glm::vec3 leafColor  = {0.28f, 0.52f, 0.24f};

    // Trunk: 4 side faces of a thin box (Z up), base at 0, top at 0.55
    const float tw = 0.09f, t0 = 0.0f, t1 = 0.55f;
    const glm::vec3 trunkCorners[4] = {
        {-tw, -tw, 0}, { tw, -tw, 0}, { tw, tw, 0}, {-tw, tw, 0}
    };
    for (int i = 0; i < 4; i++) {
        glm::vec3 a = trunkCorners[i];
        glm::vec3 b = trunkCorners[(i + 1) % 4];
        glm::vec3 n = glm::normalize(glm::vec3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, 0.0f));
        glm::vec3 a0 = {a.x, a.y, t0}, b0 = {b.x, b.y, t0};
        glm::vec3 a1 = {a.x, a.y, t1}, b1 = {b.x, b.y, t1};
        verts.push_back(makeVertex(a0, n, trunkColor, {0.0f, 1.0f}, LAYER_WOOD));
        verts.push_back(makeVertex(b0, n, trunkColor, {1.0f, 1.0f}, LAYER_WOOD));
        verts.push_back(makeVertex(b1, n, trunkColor, {1.0f, 0.0f}, LAYER_WOOD));
        verts.push_back(makeVertex(a0, n, trunkColor, {0.0f, 1.0f}, LAYER_WOOD));
        verts.push_back(makeVertex(b1, n, trunkColor, {1.0f, 0.0f}, LAYER_WOOD));
        verts.push_back(makeVertex(a1, n, trunkColor, {0.0f, 0.0f}, LAYER_WOOD));
    }

    // Canopy: 3 stacked cones
    struct Cone { float baseZ, radius, topZ; };
    const Cone cones[3] = {
        {0.35f, 0.45f, 0.95f},
        {0.70f, 0.34f, 1.25f},
        {1.00f, 0.22f, 1.55f},
    };
    const int seg = 8;
    for (const auto& cone : cones) {
        for (int i = 0; i < seg; i++) {
            float a0 = (float)i       / seg * 6.2831853f;
            float a1 = (float)(i + 1) / seg * 6.2831853f;
            glm::vec3 b0   = {cone.radius * cosf(a0), cone.radius * sinf(a0), cone.baseZ};
            glm::vec3 b1   = {cone.radius * cosf(a1), cone.radius * sinf(a1), cone.baseZ};
            glm::vec3 apex = {0.0f, 0.0f, cone.topZ};
            glm::vec3 n    = glm::normalize(glm::cross(b1 - b0, apex - b0));
            pushTri(verts, b0, b1, apex, n, leafColor, LAYER_LEAF);
        }
    }

    upload(ObjectType::TREE, verts);
    }

    // ---- ROCK: squished octahedron (low-poly boulder) ----
    {
    std::vector<ChunkVertex> verts;
    const glm::vec3 rockColor = {0.52f, 0.52f, 0.56f};
    const float r = 0.40f, mz = 0.22f, topZ = 0.50f;
    const glm::vec3 apex   = {0.0f, 0.0f, topZ};
    const glm::vec3 bottom = {0.0f, 0.0f, 0.0f};
    const glm::vec3 ring[4] = {
        { r, 0.0f, mz}, {0.0f,  r, mz}, {-r, 0.0f, mz}, {0.0f, -r, mz}
    };
    const glm::vec3 center = {0.0f, 0.0f, mz};
    // Flat-shaded triangle; normal forced to point away from the rock center
    auto tri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(n, (a + b + c) / 3.0f - center) < 0.0f) n = -n;
        pushTri(verts, a, b, c, n, rockColor, LAYER_STONE);
    };
    for (int i = 0; i < 4; i++) {
        const glm::vec3& p0 = ring[i];
        const glm::vec3& p1 = ring[(i + 1) % 4];
        tri(apex,   p0, p1); // upper face
        tri(bottom, p0, p1); // lower face
    }
    upload(ObjectType::ROCK, verts);
    }

    // Flat-shaded axis-aligned box helper (cullMode is NONE for objects, so winding is free).
    auto pushBox = [&](std::vector<ChunkVertex>& v, glm::vec3 mn, glm::vec3 mx, glm::vec3 col, float layer) {
        const glm::vec3 c[8] = {
            {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mx.y,mn.z},{mn.x,mx.y,mn.z},
            {mn.x,mn.y,mx.z},{mx.x,mn.y,mx.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},
        };
        auto quad = [&](int a, int b, int d, int e, glm::vec3 n) {
            v.push_back(makeVertex(c[a], n, col, {0.0f, 1.0f}, layer));
            v.push_back(makeVertex(c[b], n, col, {1.0f, 1.0f}, layer));
            v.push_back(makeVertex(c[d], n, col, {1.0f, 0.0f}, layer));
            v.push_back(makeVertex(c[a], n, col, {0.0f, 1.0f}, layer));
            v.push_back(makeVertex(c[d], n, col, {1.0f, 0.0f}, layer));
            v.push_back(makeVertex(c[e], n, col, {0.0f, 0.0f}, layer));
        };
        quad(4,5,6,7,{0,0,1});  quad(0,3,2,1,{0,0,-1});
        quad(0,1,5,4,{0,-1,0}); quad(3,7,6,2,{0,1,0});
        quad(1,2,6,5,{1,0,0});  quad(0,4,7,3,{-1,0,0});
    };

    // ---- WORKBENCH: a simple low table box ----
    {
    std::vector<ChunkVertex> verts;
    pushBox(verts, {-0.35f, -0.35f, 0.0f}, {0.35f, 0.35f, 0.42f}, {0.52f, 0.36f, 0.18f}, LAYER_WOOD);
    pushBox(verts, {-0.40f, -0.40f, 0.42f}, {0.40f, 0.40f, 0.52f}, {0.62f, 0.45f, 0.25f}, LAYER_WOOD); // top slab
    upload(ObjectType::WORKBENCH, verts);
    }

    // ---- FENCE: two posts + two rails ----
    {
    std::vector<ChunkVertex> verts;
    const glm::vec3 wood = {0.56f, 0.40f, 0.22f};
    pushBox(verts, {-0.40f, -0.07f, 0.0f}, {-0.24f, 0.07f, 0.6f}, wood, LAYER_WOOD); // left post
    pushBox(verts, { 0.24f, -0.07f, 0.0f}, { 0.40f, 0.07f, 0.6f}, wood, LAYER_WOOD); // right post
    pushBox(verts, {-0.40f, -0.05f, 0.40f}, {0.40f, 0.05f, 0.50f}, wood, LAYER_WOOD); // top rail
    pushBox(verts, {-0.40f, -0.05f, 0.18f}, {0.40f, 0.05f, 0.28f}, wood, LAYER_WOOD); // lower rail
    upload(ObjectType::FENCE, verts);
    }

    // ---- STONE_FENCE: low stone wall ----
    {
    std::vector<ChunkVertex> verts;
    pushBox(verts, {-0.42f, -0.14f, 0.0f}, {0.42f, 0.14f, 0.45f}, {0.56f, 0.56f, 0.60f}, LAYER_STONE);
    upload(ObjectType::STONE_FENCE, verts);
    }

    // ---- SHIP (OceanVoyage placeholder): low-poly hull + bow wedge + mast + square sail.
    // Forward is +X so object.vert's instanceRot aligns the bow with the player heading;
    // the per-frame instance seats it on the sea surface. Untextured (LAYER_NONE). ----
    {
    std::vector<ChunkVertex> verts;
    const glm::vec3 hullColor = {0.40f, 0.26f, 0.14f};
    const glm::vec3 deckColor = {0.50f, 0.36f, 0.20f};
    const glm::vec3 mastColor = {0.32f, 0.22f, 0.12f};
    const glm::vec3 sailColor = {0.88f, 0.86f, 0.80f};

    // Hull body + a small stern cabin
    pushBox(verts, {-0.70f, -0.24f, 0.00f}, { 0.55f, 0.24f, 0.26f}, hullColor, LAYER_NONE);
    pushBox(verts, {-0.66f, -0.18f, 0.26f}, {-0.40f, 0.18f, 0.40f}, deckColor, LAYER_NONE);

    // Bow wedge: taper the hull's front face (x=0.55) to a point ahead of the bow.
    const glm::vec3 P   = { 0.92f,  0.00f, 0.13f};
    const glm::vec3 ftl = { 0.55f, -0.24f, 0.26f}, ftr = { 0.55f, 0.24f, 0.26f};
    const glm::vec3 fbl = { 0.55f, -0.24f, 0.00f}, fbr = { 0.55f, 0.24f, 0.00f};
    const glm::vec3 hullCenter = {0.0f, 0.0f, 0.13f};
    auto bowTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(n, (a + b + c) / 3.0f - hullCenter) < 0.0f) n = -n; // force outward
        pushTri(verts, a, b, c, n, hullColor, LAYER_NONE);
    };
    bowTri(ftl, ftr, P); // top
    bowTri(fbr, fbl, P); // bottom
    bowTri(fbl, ftl, P); // port (-Y)
    bowTri(ftr, fbr, P); // starboard (+Y)

    // Mast + square sail (thin in X, spanning the width in Y)
    pushBox(verts, {-0.03f, -0.03f, 0.26f}, {0.03f, 0.03f, 0.92f}, mastColor, LAYER_NONE);
    pushBox(verts, {-0.02f, -0.28f, 0.34f}, {0.02f, 0.28f, 0.86f}, sailColor, LAYER_NONE);

    uploadMesh(m_shipMesh, verts);
    }

    // ---- GROUND PATCH: thin visual-only dirt/dry-grass breakup decal ----
    {
    std::vector<ChunkVertex> verts;
    const glm::vec3 n = {0.0f, 0.0f, 1.0f};
    const glm::vec3 center = {0.0f, 0.0f, 0.006f};
    const glm::vec3 ring[7] = {
        { 0.46f,  0.02f, 0.006f},
        { 0.24f,  0.25f, 0.006f},
        {-0.06f,  0.33f, 0.006f},
        {-0.40f,  0.12f, 0.006f},
        {-0.30f, -0.22f, 0.006f},
        { 0.05f, -0.31f, 0.006f},
        { 0.35f, -0.17f, 0.006f},
    };
    const glm::vec3 dryGrass = {0.42f, 0.46f, 0.24f};
    const glm::vec3 dirt     = {0.34f, 0.30f, 0.18f};
    for (int i = 0; i < 7; i++) {
        const glm::vec3 col = (i % 2 == 0) ? dryGrass : dirt;
        pushTri(verts, center, ring[i], ring[(i + 1) % 7], n, col, LAYER_NONE);
    }
    uploadMesh(m_groundPatchMesh, verts);
    }

    // ---- PEBBLE: tiny visual-only low-poly stone, not a collidable object ----
    {
    std::vector<ChunkVertex> verts;
    const glm::vec3 pebbleColor = {0.42f, 0.42f, 0.40f};
    const glm::vec3 center = {0.0f, 0.0f, 0.035f};
    const glm::vec3 top    = {0.0f, 0.0f, 0.11f};
    const glm::vec3 ring[5] = {
        { 0.16f,  0.00f, 0.025f},
        { 0.04f,  0.12f, 0.030f},
        {-0.14f,  0.08f, 0.020f},
        {-0.11f, -0.10f, 0.030f},
        { 0.08f, -0.13f, 0.022f},
    };
    auto tri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(n, (a + b + c) / 3.0f - center) < 0.0f) n = -n;
        pushTri(verts, a, b, c, n, pebbleColor, LAYER_NONE);
    };
    for (int i = 0; i < 5; i++) {
        tri(top, ring[i], ring[(i + 1) % 5]);
        tri({0.0f, 0.0f, 0.0f}, ring[(i + 1) % 5], ring[i]);
    }
    uploadMesh(m_pebbleMesh, verts);
    }

    // ---- GRASS CLUMP: visual-only dressing mesh, instanced by the renderer ----
    {
    std::vector<ChunkVertex> verts;
    auto blade = [&](float angle, float width, float height, glm::vec3 col) {
        const glm::vec3 dir  = {cosf(angle), sinf(angle), 0.0f};
        const glm::vec3 side = {-dir.y, dir.x, 0.0f};
        glm::vec3 a = side * -width;
        glm::vec3 b = side *  width;
        glm::vec3 c = dir * (width * 0.6f) + glm::vec3(0.0f, 0.0f, height);
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        pushTri(verts, a, b, c, n, col, LAYER_NONE);
    };
    blade(0.0f,      0.055f, 0.30f, {0.22f, 0.42f, 0.17f});
    blade(2.0944f,   0.050f, 0.24f, {0.27f, 0.50f, 0.20f});
    blade(4.18879f,  0.045f, 0.20f, {0.19f, 0.36f, 0.16f});
    uploadMesh(m_grassClumpMesh, verts);
    }

    // ---- GRASS CARD: small blade-field cluster, instanced by the grass pipeline ----
    {
    std::vector<GrassCardVertex> verts;
    auto bladeCard = [&](float angle, glm::vec2 base, float halfW, float h, float lean, glm::vec4 uv) {
        const glm::vec3 dir  = {cosf(angle), sinf(angle), 0.0f};
        const glm::vec3 side = dir * halfW;
        const glm::vec3 leanOff = {-dir.y * lean, dir.x * lean, 0.0f};
        const glm::vec3 b = {base.x, base.y, 0.0f};
        const glm::vec3 n    = {-dir.y, dir.x, 0.0f};

        const GrassCardVertex bl{b - side, n, {uv.x, uv.w}};
        const GrassCardVertex br{b + side, n, {uv.z, uv.w}};
        const GrassCardVertex tr{b + side * 0.42f + leanOff + glm::vec3(0.0f, 0.0f, h), n, {uv.z, uv.y}};
        const GrassCardVertex tl{b - side * 0.42f + leanOff + glm::vec3(0.0f, 0.0f, h), n, {uv.x, uv.y}};

        verts.insert(verts.end(), {bl, br, tr, bl, tr, tl});
    };
    // UV rects are measured from grass_blades/opacity.png so each card samples one atlas blade.
    bladeCard(0.10f, {-0.20f, -0.08f}, 0.054f, 0.54f,  0.038f, {0.0742f, 0.0479f, 0.1152f, 0.9648f});
    bladeCard(1.05f, { 0.02f, -0.17f}, 0.052f, 0.46f, -0.030f, {0.2061f, 0.0010f, 0.2461f, 0.4385f});
    bladeCard(2.05f, { 0.20f, -0.02f}, 0.050f, 0.47f,  0.034f, {0.3779f, 0.0127f, 0.4160f, 0.4541f});
    bladeCard(3.18f, {-0.04f,  0.15f}, 0.048f, 0.45f, -0.026f, {0.5254f, 0.0088f, 0.5566f, 0.4658f});
    bladeCard(4.15f, { 0.13f,  0.15f}, 0.056f, 0.52f,  0.030f, {0.2178f, 0.4795f, 0.2627f, 0.9990f});
    bladeCard(5.20f, {-0.17f,  0.11f}, 0.052f, 0.50f, -0.026f, {0.3799f, 0.5000f, 0.4189f, 0.9805f});
    bladeCard(0.70f, { 0.00f,  0.00f}, 0.060f, 0.52f,  0.034f, {0.4971f, 0.5127f, 0.5605f, 0.9863f});
    bladeCard(2.62f, {-0.10f, -0.19f}, 0.048f, 0.42f,  0.020f, {0.2061f, 0.0010f, 0.2461f, 0.4385f});
    bladeCard(4.72f, { 0.19f,  0.07f}, 0.048f, 0.44f, -0.020f, {0.3799f, 0.5000f, 0.4189f, 0.9805f});
    uploadGrassCardMesh(m_grassCardMesh, verts);
    }
}

// ============================================================
//  Grass blade textures
// ============================================================
void VulkanContext::createGrassTexture() {
    // Color/albedo uploads as sRGB (hardware linearizes on sample); the opacity mask
    // uploads as UNORM (raw coverage value, no gamma).
    auto uploadImage = [&](const LoadedImageRGBA8& image, VkFormat format, bool mip) {
        const VkDeviceSize imgSize = (VkDeviceSize)image.width * (VkDeviceSize)image.height * 4;
        return createTexture((uint32_t)image.width, (uint32_t)image.height,
            format, image.pixels.data(), imgSize, /*withSampler=*/true, mip);
    };
    auto makeOpacityFromAlpha = [](const LoadedImageRGBA8& source) {
        LoadedImageRGBA8 opacity;
        opacity.width = source.width;
        opacity.height = source.height;
        opacity.pixels.resize((size_t)source.width * (size_t)source.height * 4, 255);
        for (size_t i = 0, count = (size_t)source.width * (size_t)source.height; i < count; i++) {
            const uint8_t alpha = source.pixels[i * 4 + 3];
            opacity.pixels[i * 4 + 0] = alpha;
            opacity.pixels[i * 4 + 1] = alpha;
            opacity.pixels[i * 4 + 2] = alpha;
            opacity.pixels[i * 4 + 3] = 255;
        }
        return opacity;
    };

    const std::string grassColorPath = "assets/textures/vegetation/grass_blades/color.png";
    const std::string grassOpacityPath = "assets/textures/vegetation/grass_blades/opacity.png";
    const bool hasGrassColor = fileExists(grassColorPath);
    const bool hasGrassOpacity = fileExists(grassOpacityPath);
    if (hasGrassColor || hasGrassOpacity) {
        if (!hasGrassColor || !hasGrassOpacity) {
            throw std::runtime_error("Grass blade texture set requires both color.png and opacity.png");
        }

        LoadedImageRGBA8 colorImage = loadImageRGBA8(grassColorPath);
        LoadedImageRGBA8 opacityImage = loadImageRGBA8(grassOpacityPath);
        if (colorImage.width != opacityImage.width || colorImage.height != opacityImage.height) {
            throw std::runtime_error("Grass blade color/opacity texture size mismatch");
        }

        m_grassTex = uploadImage(colorImage, VK_FORMAT_R8G8B8A8_SRGB, /*mipmapped=*/true);
        m_grassOpacityTex = uploadImage(opacityImage, VK_FORMAT_R8G8B8A8_UNORM, /*mipmapped=*/false);
        return;
    }

    const std::string authoredGrassPath = "assets/textures/grass.png";
    if (fileExists(authoredGrassPath)) {
        LoadedImageRGBA8 image = loadImageRGBA8(authoredGrassPath);
        m_grassTex = uploadImage(image, VK_FORMAT_R8G8B8A8_SRGB, /*mipmapped=*/true);
        m_grassOpacityTex = uploadImage(makeOpacityFromAlpha(image), VK_FORMAT_R8G8B8A8_UNORM, /*mipmapped=*/false);
        return;
    }

    // A slim grass-blade mask: narrow tapering blades drawn into alpha, with a
    // base-dark / tip-light green. Kept as a fallback when authored foliage textures
    // are not present.
    const uint32_t W = 64, H = 64;
    std::vector<uint8_t> pixels((size_t)W * H * 4, 0); // RGBA8, fully transparent

    auto clamp01 = [](float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    auto smooth = [](float t) {
        return t * t * (3.0f - 2.0f * t);
    };
    auto writePixel = [&](int x, int y, const glm::vec3& c, float alpha) {
        if (x < 0 || x >= (int)W || y < 0 || y >= (int)H) return;
        uint8_t* p = &pixels[((size_t)y * W + x) * 4];
        const float a = clamp01(alpha);
        const float oldA = (float)p[3] / 255.0f;
        if (a < oldA * 0.85f) return;
        p[0] = (uint8_t)(c.r * 255.0f);
        p[1] = (uint8_t)(c.g * 255.0f);
        p[2] = (uint8_t)(c.b * 255.0f);
        p[3] = (uint8_t)(std::max(oldA, a) * 255.0f);
    };

    struct Blade { float baseX, midX, tipX, height, halfW, shade; };
    static const Blade blades[] = {
        {0.50f, 0.50f, 0.54f, 1.00f, 0.075f, 1.08f},
        {0.40f, 0.34f, 0.24f, 0.84f, 0.052f, 0.94f},
        {0.60f, 0.66f, 0.78f, 0.80f, 0.052f, 1.02f},
        {0.46f, 0.41f, 0.32f, 0.62f, 0.040f, 0.88f},
        {0.55f, 0.62f, 0.72f, 0.58f, 0.040f, 0.96f},
        {0.32f, 0.25f, 0.16f, 0.54f, 0.034f, 0.82f},
        {0.68f, 0.76f, 0.88f, 0.52f, 0.034f, 0.86f},
    };
    const glm::vec3 baseCol = {0.16f, 0.34f, 0.12f};
    const glm::vec3 tipCol  = {0.58f, 0.72f, 0.26f};

    for (const Blade& b : blades) {
        for (uint32_t y = 0; y < H; y++) {
            const float t = 1.0f - (float)y / (float)(H - 1); // 0 at bottom row, 1 at top row
            if (t > b.height) continue;

            const float u = t / b.height;
            const float su = smooth(u);
            const float cx = glm::mix(glm::mix(b.baseX, b.midX, su), b.tipX, su * su);
            const float halfW = b.halfW * powf(1.0f - u, 1.45f);
            const float feather = 1.25f / (float)W;
            const glm::vec3 col = glm::mix(baseCol, tipCol, smooth(u)) * b.shade;

            const int x0 = (int)((cx - halfW - feather) * W);
            const int x1 = (int)((cx + halfW + feather) * W);
            for (int x = x0; x <= x1; x++) {
                const float px = ((float)x + 0.5f) / (float)W;
                const float dist = fabsf(px - cx);
                const float a = clamp01((halfW + feather - dist) / feather);
                writePixel(x, (int)y, col, a * (0.76f + 0.24f * u));
            }
        }
    }

    std::vector<uint8_t> opacityPixels((size_t)W * H * 4, 255);
    for (size_t i = 0, count = (size_t)W * H; i < count; i++) {
        const uint8_t alpha = pixels[i * 4 + 3];
        opacityPixels[i * 4 + 0] = alpha;
        opacityPixels[i * 4 + 1] = alpha;
        opacityPixels[i * 4 + 2] = alpha;
        opacityPixels[i * 4 + 3] = 255;
    }

    // Upload the procedural pixels through the shared texture helper. The grass card
    // pipeline samples these with their own LINEAR/CLAMP samplers (withSampler=true).
    const VkDeviceSize imgSize = (VkDeviceSize)W * H * 4;
    m_grassTex = createTexture(W, H, VK_FORMAT_R8G8B8A8_SRGB, pixels.data(), imgSize, /*withSampler=*/true, /*mipmapped=*/true);
    m_grassOpacityTex = createTexture(W, H, VK_FORMAT_R8G8B8A8_UNORM, opacityPixels.data(), imgSize, /*withSampler=*/true);
}

TextureResource VulkanContext::createTextureArray(uint32_t width, uint32_t height, uint32_t layerCount,
    VkFormat format, const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped)
{
    TextureResource tex;
    tex.device = m_device;

    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, bytes, (size_t)size);
    vkUnmapMemory(m_device, staging.memory);

    const uint32_t mipLevels = mipmapped
        ? (uint32_t)std::floor(std::log2((float)std::max(width, height))) + 1u
        : 1u;

    // createImage hardcodes arrayLayers=1, so build the array image inline here.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent        = {width, height, 1};
    imageInfo.mipLevels     = mipLevels;
    imageInfo.arrayLayers   = layerCount;
    imageInfo.format        = format;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              (mipmapped ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture array image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate texture array memory");
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    // One-shot upload: transition all layers, copy the contiguous layer-major buffer, transition to read.
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandPool        = m_commandPool;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &cbAlloc, &cmd);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = layerCount;

    barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = layerCount;
    region.imageExtent                 = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // For mipmapped arrays, generateMipmaps() below transitions every level to SHADER_READ.
    if (!mipmapped) {
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);
    vkQueueSubmit(m_graphicsQueue, 1, &submit, fence);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    // staging frees here (GpuBuffer RAII); the fence already waited on all transfers.

    if (mipmapped)
        generateMipmaps(tex.image, format, (int32_t)width, (int32_t)height, mipLevels, layerCount);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = tex.image;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = mipLevels;
    vi.subresourceRange.layerCount = layerCount;
    if (vkCreateImageView(m_device, &vi, nullptr, &tex.view) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture array view");

    if (withSampler) {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR; // trilinear
        si.minLod       = 0.0f;
        si.maxLod       = (float)mipLevels;
        si.anisotropyEnable = m_anisotropyEnabled ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy    = m_maxAnisotropy;
        if (vkCreateSampler(m_device, &si, nullptr, &tex.sampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create texture array sampler");
    }

    return tex;
}

void VulkanContext::createTerrainTextureArray() {
    struct TerrainLayerFile {
        uint32_t layer;
        const char* path;
    };
    static const TerrainLayerFile terrainLayerFiles[] = {
        {0, "assets/textures/terrain/grass_top.png"},
        {1, "assets/textures/terrain/grass_side.png"},
        {2, "assets/textures/terrain/dirt.png"},
        {3, "assets/textures/terrain/stone.png"},
        {4, "assets/textures/terrain/wood.png"},
        {5, "assets/textures/terrain/leaves.png"},
        {6, "assets/textures/terrain/farmland.png"},
        {7, "assets/textures/terrain/wheat.png"},
        {8, "assets/textures/terrain/water.png"},
    };

    // Step 4b: authored terrain images override the procedural material masks per layer.
    uint32_t W = 64, H = 64;
    for (const TerrainLayerFile& file : terrainLayerFiles) {
        if (!fileExists(file.path)) continue;
        LoadedImageRGBA8 image = loadImageRGBA8(file.path);
        W = (uint32_t)image.width;
        H = (uint32_t)image.height;
        break;
    }

    const uint32_t L = TERRAIN_TEX_LAYERS;
    std::vector<uint8_t> pixels((size_t)W * H * 4 * L, 255);

    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    auto lerp   = [](float a, float b, float t) { return a + (b - a) * t; };
    auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    auto fract = [](float v) { return v - std::floor(v); };
    auto hash01 = [](int x, int y, int salt) -> float {
        uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)salt * 83492791u;
        h ^= h >> 13; h *= 1274126177u;
        return (float)(h & 65535u) / 65535.0f;
    };
    // Tileable value noise: lattice coords wrap modulo (size/cell) so texture edges match.
    auto tileNoise = [&](int x, int y, int cell, int salt) -> float {
        const int cells = (int)W / cell;
        auto wh = [&](int gx, int gy) { return hash01(((gx % cells) + cells) % cells, ((gy % cells) + cells) % cells, salt); };
        const int gx = x / cell, gy = y / cell;
        const float tx = smooth((float)(x - gx * cell) / (float)cell);
        const float ty = smooth((float)(y - gy * cell) / (float)cell);
        return lerp(lerp(wh(gx, gy), wh(gx + 1, gy), tx),
                    lerp(wh(gx, gy + 1), wh(gx + 1, gy + 1), tx), ty);
    };
    auto fbm = [&](int x, int y, int salt) {
        return tileNoise(x, y, 16, salt) * 0.50f +
               tileNoise(x, y,  8, salt + 1) * 0.32f +
               tileNoise(x, y,  4, salt + 2) * 0.18f;
    };
    auto wave = [](float t, float freq, float phase = 0.0f) {
        return 0.5f + 0.5f * std::sin((t * freq + phase) * 6.28318530718f);
    };
    auto thinLine = [&](float t, float freq, float phase = 0.0f) {
        float d = std::abs(fract(t * freq + phase) - 0.5f) * 2.0f;
        return std::pow(1.0f - d, 9.0f);
    };
    auto writePixel = [&](uint32_t layer, uint32_t x, uint32_t y, glm::vec3 c) {
        uint8_t* p = &pixels[(((size_t)layer * H + y) * W + x) * 4];
        p[0] = (uint8_t)(clamp01(c.r) * 255.0f);
        p[1] = (uint8_t)(clamp01(c.g) * 255.0f);
        p[2] = (uint8_t)(clamp01(c.b) * 255.0f);
        p[3] = 255;
    };

    for (uint32_t layer = 0; layer < L; layer++) {
        const int salt = 100 + (int)layer * 37;
        for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            const float u = ((float)x + 0.5f) / (float)W;
            const float v = ((float)y + 0.5f) / (float)H;
            const float n = fbm((int)x, (int)y, salt);
            const float fine = tileNoise((int)x, (int)y, 2, salt + 7);

            glm::vec3 c(0.9f);
            switch (layer) {
                case 0: { // Grass top: soft blades and clumps.
                    float blades = wave(u + tileNoise((int)x, (int)y, 8, salt + 11) * 0.10f, 18.0f);
                    float value = 0.78f + n * 0.15f + blades * 0.07f + fine * 0.03f;
                    c = glm::vec3(value * 0.96f, value * 1.02f, value * 0.93f);
                    break;
                }
                case 1: { // Grass side: dirt strata with a little root breakup.
                    float strata = wave(v + n * 0.07f, 5.0f);
                    float value = 0.72f + n * 0.15f + strata * 0.07f;
                    c = glm::vec3(value * 1.02f, value * 0.93f, value * 0.82f);
                    break;
                }
                case 2: { // Dirt: clods and small darker grains.
                    float speck = hash01((int)x, (int)y, salt + 19) > 0.78f ? 1.0f : 0.0f;
                    float value = 0.70f + n * 0.22f + fine * 0.05f - speck * 0.07f;
                    c = glm::vec3(value * 1.03f, value * 0.92f, value * 0.78f);
                    break;
                }
                case 3: { // Stone: mottled facets and hairline cracks.
                    float crack = std::max(thinLine(u + n * 0.05f, 4.0f), thinLine(v + n * 0.05f, 4.0f));
                    float value = 0.72f + n * 0.20f + fine * 0.04f - crack * 0.13f;
                    c = glm::vec3(value * 0.96f, value * 0.98f, value * 1.02f);
                    break;
                }
                case 4: { // Wood: broad grain lines.
                    float grain = wave(u + tileNoise((int)x, (int)y, 16, salt + 23) * 0.18f, 9.0f);
                    float ring = thinLine(u + n * 0.08f, 4.0f);
                    float value = 0.68f + grain * 0.16f + n * 0.12f - ring * 0.06f;
                    c = glm::vec3(value * 1.06f, value * 0.91f, value * 0.70f);
                    break;
                }
                case 5: { // Leaves: clustered mottling.
                    float spot = hash01((int)(x / 2), (int)(y / 2), salt + 31) > 0.70f ? 1.0f : 0.0f;
                    float value = 0.76f + n * 0.17f + fine * 0.06f - spot * 0.06f;
                    c = glm::vec3(value * 0.92f, value * 1.03f, value * 0.86f);
                    break;
                }
                case 6: { // Farmland: tilled furrows.
                    float furrow = wave(v + n * 0.04f, 6.0f);
                    float darkLine = std::pow(1.0f - furrow, 3.0f);
                    float value = 0.68f + n * 0.12f + furrow * 0.08f - darkLine * 0.13f;
                    c = glm::vec3(value * 1.03f, value * 0.88f, value * 0.68f);
                    break;
                }
                case 7: { // Wheat: thin stalk rhythm.
                    float stalks = std::pow(wave(u + n * 0.07f, 14.0f), 2.5f);
                    float value = 0.76f + n * 0.12f + stalks * 0.14f;
                    c = glm::vec3(value * 1.06f, value * 0.98f, value * 0.70f);
                    break;
                }
                case 8: { // Water: subtle placeholder ripples; a dedicated water pass comes later.
                    float ripple = wave(u + v + n * 0.03f, 2.0f) * 0.55f +
                                   wave(u - v + n * 0.03f, 3.0f) * 0.45f;
                    float value = 0.83f + ripple * 0.04f + n * 0.025f;
                    c = glm::vec3(value * 0.88f, value * 0.98f, value * 1.03f);
                    break;
                }
                default: {
                    float value = 0.74f + n * 0.24f;
                    c = glm::vec3(value);
                    break;
                }
            }

            writePixel(layer, x, y, c);
        }
    }

    auto copyLayerFromFile = [&](const TerrainLayerFile& file) {
        if (!fileExists(file.path)) return;

        LoadedImageRGBA8 image = loadImageRGBA8(file.path);
        if ((uint32_t)image.width != W || (uint32_t)image.height != H) {
            std::cerr << "Skipping terrain texture with mismatched size: " << file.path
                      << " (" << image.width << "x" << image.height
                      << ", expected " << W << "x" << H << ")\n";
            return;
        }

        const size_t layerOffset = (size_t)file.layer * W * H * 4;
        const size_t byteCount = (size_t)W * H * 4;
        memcpy(pixels.data() + layerOffset, image.pixels.data(), byteCount);
    };

    for (const TerrainLayerFile& file : terrainLayerFiles) {
        if (file.layer < L) copyLayerFromFile(file);
    }

    const VkDeviceSize size = (VkDeviceSize)W * H * 4 * L;
    m_terrainTex = createTextureArray(W, H, L, VK_FORMAT_R8G8B8A8_SRGB, pixels.data(), size, /*withSampler=*/true, /*mipmapped=*/true);
}

// ============================================================
//  Framebuffers
// ============================================================
void VulkanContext::createFramebuffers() {
    // Scene framebuffers — per-frame offscreen color + shared depth
    m_sceneFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView attachments[] = { m_offscreenView[i], m_depthImageView };
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_renderPass;
        info.attachmentCount = 2;
        info.pAttachments    = attachments;
        info.width           = m_swapchainExtent.width;
        info.height          = m_swapchainExtent.height;
        info.layers          = 1;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_sceneFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene framebuffer");
    }

    // Planar reflection framebuffers — mirrored scene color + shared depth.
    m_reflectionFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView attachments[] = { m_reflectionView[i], m_depthImageView };
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_renderPass;
        info.attachmentCount = 2;
        info.pAttachments    = attachments;
        info.width           = m_swapchainExtent.width;
        info.height          = m_swapchainExtent.height;
        info.layers          = 1;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_reflectionFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create planar reflection framebuffer");
    }

    // Post framebuffers — one per swapchain image
    m_postFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        VkImageView attachments[] = { m_swapchainImageViews[i] };
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_postRenderPass;
        info.attachmentCount = 1;
        info.pAttachments    = attachments;
        info.width           = m_swapchainExtent.width;
        info.height          = m_swapchainExtent.height;
        info.layers          = 1;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_postFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post framebuffer");
    }

    m_smaaEdgeFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_smaaBlendFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_smaaRenderPass;
        info.attachmentCount = 1;
        info.width           = m_swapchainExtent.width;
        info.height          = m_swapchainExtent.height;
        info.layers          = 1;

        VkImageView edgeAttachment[] = { m_smaaEdgeView[i] };
        info.pAttachments = edgeAttachment;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_smaaEdgeFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SMAA edge framebuffer");

        VkImageView blendAttachment[] = { m_smaaBlendView[i] };
        info.pAttachments = blendAttachment;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_smaaBlendFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SMAA blend framebuffer");
    }
}

// ============================================================
//  Post-process: offscreen target, render pass, pipeline, descriptors
// ============================================================
void VulkanContext::createPostRenderPass() {
    VkAttachmentDescription color{};
    color.format         = m_swapchainFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fullscreen pass overwrites every pixel
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dep;
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_postRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post render pass");
}

void VulkanContext::createSmaaRenderPass() {
    VkAttachmentDescription color{};
    color.format         = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 2;
    info.pDependencies   = deps;
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_smaaRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SMAA render pass");
}

void VulkanContext::createOffscreenResources() {
    m_offscreenImage.resize(MAX_FRAMES_IN_FLIGHT);
    m_offscreenMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_offscreenView.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createImage(m_swapchainExtent.width, m_swapchainExtent.height, m_sceneColorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_offscreenImage[i], m_offscreenMemory[i]);

        VkImageViewCreateInfo v{};
        v.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        v.image                           = m_offscreenImage[i];
        v.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        v.format                          = m_sceneColorFormat;
        v.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        v.subresourceRange.levelCount     = 1;
        v.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(m_device, &v, nullptr, &m_offscreenView[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create offscreen image view");
    }
}

void VulkanContext::createPlanarReflectionResources() {
    m_reflectionImage.resize(MAX_FRAMES_IN_FLIGHT);
    m_reflectionMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_reflectionView.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createImage(m_swapchainExtent.width, m_swapchainExtent.height, m_sceneColorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_reflectionImage[i], m_reflectionMemory[i]);

        VkImageViewCreateInfo v{};
        v.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        v.image                           = m_reflectionImage[i];
        v.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        v.format                          = m_sceneColorFormat;
        v.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        v.subresourceRange.levelCount     = 1;
        v.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(m_device, &v, nullptr, &m_reflectionView[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create planar reflection image view");
    }
}

void VulkanContext::createSmaaResources() {
    auto createTarget = [&](std::vector<VkImage>& images,
                            std::vector<VkDeviceMemory>& memories,
                            std::vector<VkImageView>& views,
                            const char* label)
    {
        images.resize(MAX_FRAMES_IN_FLIGHT);
        memories.resize(MAX_FRAMES_IN_FLIGHT);
        views.resize(MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            createImage(m_swapchainExtent.width, m_swapchainExtent.height, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                images[i], memories[i]);

            VkImageViewCreateInfo v{};
            v.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            v.image                           = images[i];
            v.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            v.format                          = VK_FORMAT_R8G8B8A8_UNORM;
            v.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            v.subresourceRange.levelCount     = 1;
            v.subresourceRange.layerCount     = 1;
            if (vkCreateImageView(m_device, &v, nullptr, &views[i]) != VK_SUCCESS)
                throw std::runtime_error(std::string("Failed to create ") + label + " image view");
        }
    };

    createTarget(m_smaaEdgeImage, m_smaaEdgeMemory, m_smaaEdgeView, "SMAA edge");
    createTarget(m_smaaBlendImage, m_smaaBlendMemory, m_smaaBlendView, "SMAA blend");
}

TextureResource VulkanContext::createTexture(uint32_t width, uint32_t height, VkFormat format,
    const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped)
{
    TextureResource tex;
    tex.device = m_device;

    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, bytes, (size_t)size);
    vkUnmapMemory(m_device, staging.memory);

    const uint32_t mipLevels = mipmapped
        ? (uint32_t)std::floor(std::log2((float)std::max(width, height))) + 1u
        : 1u;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mipmapped) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // blit source for mip generation
    createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL, usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory, mipLevels);

    transitionImageLayout(tex.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); // mip 0
    copyBufferToImage(staging.buffer, tex.image, width, height);
    if (mipmapped)
        generateMipmaps(tex.image, format, (int32_t)width, (int32_t)height, mipLevels, 1);
    else
        transitionImageLayout(tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // staging frees here (GpuBuffer RAII); all transfers already waited on a fence.

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = tex.image;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = mipLevels;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &vi, nullptr, &tex.view) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture view");

    if (withSampler) {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR; // trilinear
        si.minLod       = 0.0f;
        si.maxLod       = (float)mipLevels;
        si.anisotropyEnable = m_anisotropyEnabled ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy    = m_maxAnisotropy;
        if (vkCreateSampler(m_device, &si, nullptr, &tex.sampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create texture sampler");
    }

    return tex;
}

void VulkanContext::createSmaaLookupTextures() {
    // SMAA LUTs are sampled through m_postSampler, so no per-texture sampler.
    m_smaaAreaTex = createTexture(AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM,
        areaTexBytes, AREATEX_SIZE, /*withSampler=*/false);
    m_smaaSearchTex = createTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM,
        searchTexBytes, SEARCHTEX_SIZE, /*withSampler=*/false);
}

void VulkanContext::createPostPipeline() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{};
    dl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 1;
    dl.pBindings    = &binding;
    if (vkCreateDescriptorSetLayout(m_device, &dl, nullptr, &m_postDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post descriptor set layout");

    VkPipelineLayoutCreateInfo pl{};
    VkPushConstantRange postPush{};
    postPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    postPush.offset     = 0;
    postPush.size       = sizeof(PostPushConstants);
    pl.sType                   = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount          = 1;
    pl.pSetLayouts             = &m_postDescriptorSetLayout;
    pl.pushConstantRangeCount  = 1;
    pl.pPushConstantRanges     = &postPush;
    if (vkCreatePipelineLayout(m_device, &pl, nullptr, &m_postPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post pipeline layout");

    auto vertCode = readFile("shaders/post.vert.spv");
    auto fragCode = readFile("shaders/post.frag.spv");
    VkShaderModule vertMod = createShaderModule(vertCode);
    VkShaderModule fragMod = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{}; // no vertex buffers (gl_VertexIndex)
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0, 0, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
    VkRect2D   scissor{ {0, 0}, m_swapchainExtent };
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttach;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState   = &msaa;
    pipelineInfo.pColorBlendState    = &blend;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_postPipelineLayout;
    pipelineInfo.renderPass          = m_postRenderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_postPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
}

void VulkanContext::createSmaaPipelines() {
    auto createSetLayout = [&](std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
                               VkDescriptorSetLayout& outLayout,
                               const char* label)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindingVec(bindings);
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = (uint32_t)bindingVec.size();
        info.pBindings    = bindingVec.data();
        if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &outLayout) != VK_SUCCESS)
            throw std::runtime_error(std::string("Failed to create ") + label + " descriptor set layout");
    };

    VkDescriptorSetLayoutBinding sampled{};
    sampled.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampled.descriptorCount = 1;
    sampled.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding edgeScene = sampled;
    edgeScene.binding = 0;
    createSetLayout({ edgeScene }, m_smaaEdgeDescriptorSetLayout, "SMAA edge");

    VkDescriptorSetLayoutBinding blendEdges = sampled;
    blendEdges.binding = 0;
    VkDescriptorSetLayoutBinding blendArea = sampled;
    blendArea.binding = 1;
    VkDescriptorSetLayoutBinding blendSearch = sampled;
    blendSearch.binding = 2;
    createSetLayout({ blendEdges, blendArea, blendSearch }, m_smaaBlendDescriptorSetLayout, "SMAA blend");

    VkDescriptorSetLayoutBinding neighborhoodScene = sampled;
    neighborhoodScene.binding = 0;
    VkDescriptorSetLayoutBinding neighborhoodBlend = sampled;
    neighborhoodBlend.binding = 1;
    createSetLayout({ neighborhoodScene, neighborhoodBlend },
        m_smaaNeighborhoodDescriptorSetLayout, "SMAA neighborhood");

    auto createLayout = [&](VkDescriptorSetLayout setLayout,
                            VkPipelineLayout& outLayout,
                            const char* label)
    {
        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset     = 0;
        push.size       = sizeof(PostPushConstants);

        VkPipelineLayoutCreateInfo info{};
        info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount         = 1;
        info.pSetLayouts            = &setLayout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges    = &push;
        if (vkCreatePipelineLayout(m_device, &info, nullptr, &outLayout) != VK_SUCCESS)
            throw std::runtime_error(std::string("Failed to create ") + label + " pipeline layout");
    };

    createLayout(m_smaaEdgeDescriptorSetLayout, m_smaaEdgePipelineLayout, "SMAA edge");
    createLayout(m_smaaBlendDescriptorSetLayout, m_smaaBlendPipelineLayout, "SMAA blend");
    createLayout(m_smaaNeighborhoodDescriptorSetLayout, m_smaaNeighborhoodPipelineLayout, "SMAA neighborhood");

    auto createFullscreenPipeline = [&](const char* fragPath,
                                        VkPipelineLayout layout,
                                        VkRenderPass renderPass,
                                        VkPipeline& outPipeline,
                                        const char* label)
    {
        auto vertCode = readFile("shaders/post.vert.spv");
        auto fragCode = readFile(fragPath);
        VkShaderModule vertMod = createShaderModule(vertCode);
        VkShaderModule fragMod = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName  = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{ 0, 0, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, m_swapchainExtent };
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates    = dynamicStates;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttach{};
        blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAttach;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = stages;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState   = &msaa;
        pipelineInfo.pColorBlendState    = &blend;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = layout;
        pipelineInfo.renderPass          = renderPass;
        pipelineInfo.subpass             = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS)
            throw std::runtime_error(std::string("Failed to create ") + label + " pipeline");

        vkDestroyShaderModule(m_device, vertMod, nullptr);
        vkDestroyShaderModule(m_device, fragMod, nullptr);
    };

    createFullscreenPipeline("shaders/smaa_edge.frag.spv",
        m_smaaEdgePipelineLayout, m_smaaRenderPass, m_smaaEdgePipeline, "SMAA edge");
    createFullscreenPipeline("shaders/smaa_blend.frag.spv",
        m_smaaBlendPipelineLayout, m_smaaRenderPass, m_smaaBlendPipeline, "SMAA blend");
    createFullscreenPipeline("shaders/smaa_neighborhood.frag.spv",
        m_smaaNeighborhoodPipelineLayout, m_postRenderPass, m_smaaNeighborhoodPipeline, "SMAA neighborhood");
}

void VulkanContext::createPostSampler() {
    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = VK_FILTER_LINEAR;
    info.minFilter    = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(m_device, &info, nullptr, &m_postSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post sampler");
}

void VulkanContext::updatePostDescriptors() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo img{};
        img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img.imageView   = m_offscreenView[i];
        img.sampler     = m_postSampler;
        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = m_postDescriptorSets[i];
        w.dstBinding      = 0;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo      = &img;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
}

void VulkanContext::createPostDescriptors() {
    VkDescriptorPoolSize ps{};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = MAX_FRAMES_IN_FLIGHT;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &ps;
    pi.maxSets       = MAX_FRAMES_IN_FLIGHT;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_postDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post descriptor pool");

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_postDescriptorSetLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_postDescriptorPool;
    ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    ai.pSetLayouts        = layouts.data();
    m_postDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &ai, m_postDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate post descriptor sets");

    updatePostDescriptors();
}

void VulkanContext::updateSmaaDescriptors() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo edgeScene{};
        edgeScene.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        edgeScene.imageView   = m_offscreenView[i];
        edgeScene.sampler     = m_postSampler;

        VkWriteDescriptorSet edgeWrite{};
        edgeWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        edgeWrite.dstSet          = m_smaaEdgeDescriptorSets[i];
        edgeWrite.dstBinding      = 0;
        edgeWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        edgeWrite.descriptorCount = 1;
        edgeWrite.pImageInfo      = &edgeScene;

        VkDescriptorImageInfo blendImages[3]{};
        blendImages[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        blendImages[0].imageView   = m_smaaEdgeView[i];
        blendImages[0].sampler     = m_postSampler;
        blendImages[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        blendImages[1].imageView   = m_smaaAreaTex.view;
        blendImages[1].sampler     = m_postSampler;
        blendImages[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        blendImages[2].imageView   = m_smaaSearchTex.view;
        blendImages[2].sampler     = m_postSampler;

        VkWriteDescriptorSet blendWrites[3]{};
        for (uint32_t b = 0; b < 3; b++) {
            blendWrites[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            blendWrites[b].dstSet          = m_smaaBlendDescriptorSets[i];
            blendWrites[b].dstBinding      = b;
            blendWrites[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            blendWrites[b].descriptorCount = 1;
            blendWrites[b].pImageInfo      = &blendImages[b];
        }

        VkDescriptorImageInfo neighborhoodImages[2]{};
        neighborhoodImages[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        neighborhoodImages[0].imageView   = m_offscreenView[i];
        neighborhoodImages[0].sampler     = m_postSampler;
        neighborhoodImages[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        neighborhoodImages[1].imageView   = m_smaaBlendView[i];
        neighborhoodImages[1].sampler     = m_postSampler;

        VkWriteDescriptorSet neighborhoodWrites[2]{};
        for (uint32_t b = 0; b < 2; b++) {
            neighborhoodWrites[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            neighborhoodWrites[b].dstSet          = m_smaaNeighborhoodDescriptorSets[i];
            neighborhoodWrites[b].dstBinding      = b;
            neighborhoodWrites[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            neighborhoodWrites[b].descriptorCount = 1;
            neighborhoodWrites[b].pImageInfo      = &neighborhoodImages[b];
        }

        vkUpdateDescriptorSets(m_device, 1, &edgeWrite, 0, nullptr);
        vkUpdateDescriptorSets(m_device, 3, blendWrites, 0, nullptr);
        vkUpdateDescriptorSets(m_device, 2, neighborhoodWrites, 0, nullptr);
    }
}

void VulkanContext::createSmaaDescriptors() {
    VkDescriptorPoolSize ps{};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = MAX_FRAMES_IN_FLIGHT * (1 + 3 + 2);

    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &ps;
    pi.maxSets       = MAX_FRAMES_IN_FLIGHT * 3;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_smaaDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SMAA descriptor pool");

    auto allocate = [&](VkDescriptorSetLayout layout,
                        std::vector<VkDescriptorSet>& sets,
                        const char* label)
    {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, layout);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_smaaDescriptorPool;
        ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        ai.pSetLayouts        = layouts.data();
        sets.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(m_device, &ai, sets.data()) != VK_SUCCESS)
            throw std::runtime_error(std::string("Failed to allocate ") + label + " descriptor sets");
    };

    allocate(m_smaaEdgeDescriptorSetLayout, m_smaaEdgeDescriptorSets, "SMAA edge");
    allocate(m_smaaBlendDescriptorSetLayout, m_smaaBlendDescriptorSets, "SMAA blend");
    allocate(m_smaaNeighborhoodDescriptorSetLayout, m_smaaNeighborhoodDescriptorSets, "SMAA neighborhood");

    updateSmaaDescriptors();
}

// ============================================================
//  Command pool + buffers
// ============================================================
void VulkanContext::createCommandPool() {
    auto indices = findQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = *indices.graphics;
    if (vkCreateCommandPool(m_device, &info, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
}

#ifdef PASTEL_DEV_BUILD
void VulkanContext::createDevTools() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets       = 1000 * (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes    = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_devDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create dev descriptor pool");

    std::vector<VkQueueFamilyProperties> queueProps;
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, nullptr);
    queueProps.resize(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, queueProps.data());
    auto indices = findQueueFamilies(m_physicalDevice);
    if (indices.graphics && queueProps[*indices.graphics].timestampValidBits > 0) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        m_devTimestampPeriod  = props.limits.timestampPeriod;
        m_devTimingSupported  = true;

        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = MAX_FRAMES_IN_FLIGHT * DEV_TIMESTAMP_COUNT;
        if (vkCreateQueryPool(m_device, &queryInfo, nullptr, &m_devQueryPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create dev query pool");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(m_window.handle(), false);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance        = m_instance;
    initInfo.PhysicalDevice  = m_physicalDevice;
    initInfo.Device          = m_device;
    initInfo.QueueFamily     = *indices.graphics;
    initInfo.Queue           = m_graphicsQueue;
    initInfo.PipelineCache   = VK_NULL_HANDLE;
    initInfo.DescriptorPool  = m_devDescriptorPool;
    initInfo.Allocator       = nullptr;
    initInfo.MinImageCount   = 2;
    initInfo.ImageCount      = (uint32_t)m_swapchainImages.size();
    initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = nullptr;
    if (!ImGui_ImplVulkan_Init(&initInfo, m_postRenderPass))
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void VulkanContext::destroyDevTools() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (m_devQueryPool) {
        vkDestroyQueryPool(m_device, m_devQueryPool, nullptr);
        m_devQueryPool = VK_NULL_HANDLE;
    }
    if (m_devDescriptorPool) {
        vkDestroyDescriptorPool(m_device, m_devDescriptorPool, nullptr);
        m_devDescriptorPool = VK_NULL_HANDLE;
    }
}
#endif

void VulkanContext::createCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = m_commandPool;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    if (vkAllocateCommandBuffers(m_device, &info, m_commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffers");
}

// ============================================================
//  Sync objects
// ============================================================
void VulkanContext::createSyncObjects() {
    m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinished.resize(m_swapchainImages.size());        // present wait — one per image
    m_imagesInFlight.assign(m_swapchainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signaled so frame 0 doesn't wait forever

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(m_device, &semInfo,   nullptr, &m_imageAvailable[i]);
        vkCreateFence    (m_device, &fenceInfo, nullptr, &m_inFlight[i]);
    }
    for (auto& sem : m_renderFinished)
        vkCreateSemaphore(m_device, &semInfo, nullptr, &sem);
}

// ============================================================
//  Depth resources
// ============================================================
void VulkanContext::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    createImage(m_swapchainExtent.width, m_swapchainExtent.height, depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_depthImage, m_depthImageMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create depth image view");
}

// ============================================================
//  Shadow map resources (image, render pass, framebuffer)
// ============================================================
void VulkanContext::createShadowResources() {
    VkFormat depthFormat = findDepthFormat();

    createImage(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_shadowImage, m_shadowImageMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_shadowImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_shadowImageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow image view");

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = depthFormat;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    // Dependency: wait for previous shadow read before writing depth again
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass      = 0;
    deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Dependency: depth write must finish before the main pass samples it
    deps[1].srcSubpass      = 0;
    deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask    = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &depthAttachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies   = deps;
    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_shadowRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow render pass");

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_shadowRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &m_shadowImageView;
    fbInfo.width           = SHADOW_MAP_SIZE;
    fbInfo.height          = SHADOW_MAP_SIZE;
    fbInfo.layers          = 1;
    if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_shadowFramebuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow framebuffer");
}

// ============================================================
//  Shadow pipeline (depth-only, push constant light MVP)
// ============================================================
void VulkanContext::createShadowPipeline() {
    auto vert = readFile("shaders/shadow.vert.spv");
    VkShaderModule vertMod = createShaderModule(vert);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertMod;
    vertStage.pName  = "main";

    // ChunkVertex binding — shadow shader only reads pos (location 0)
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(ChunkVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.binding  = 0;
    attr.location = 0;
    attr.format   = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 1;
    vertInput.pVertexBindingDescriptions      = &binding;
    vertInput.vertexAttributeDescriptionCount = 1;
    vertInput.pVertexAttributeDescriptions    = &attr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0.0f, 1.0f};
    VkRect2D   scissor{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode                = VK_CULL_MODE_NONE; // NONE for tighter shadow contact (was FRONT; shader/pipeline bias controls acne)
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasSlopeFactor    = 0.0f;
    rasterizer.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow pipeline layout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 1;
    pipelineInfo.pStages             = &vertStage;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.layout              = m_shadowPipelineLayout;
    pipelineInfo.renderPass          = m_shadowRenderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
}

// ============================================================
//  Shadow pipeline for instanced objects (trees)
// ============================================================
void VulkanContext::createShadowObjectPipeline() {
    auto vert = readFile("shaders/shadow_object.vert.spv");
    VkShaderModule vertMod = createShaderModule(vert);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertMod;
    vertStage.pName  = "main";

    // Tree mesh (ChunkVertex pos) + per-instance transform (ObjectInstance)
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(ChunkVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(ObjectInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, pos)      };
    attrs[1] = { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ObjectInstance, pos)   };
    attrs[2] = { 4, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, scale) };
    attrs[3] = { 5, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, rot)   };

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 2;
    vertInput.pVertexBindingDescriptions      = bindings;
    vertInput.vertexAttributeDescriptionCount = 4;
    vertInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0.0f, 1.0f};
    VkRect2D   scissor{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode                = VK_CULL_MODE_NONE;  // tree mesh isn't watertight
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor    = 1.2f;
    rasterizer.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 1;
    pipelineInfo.pStages             = &vertStage;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.layout              = m_shadowPipelineLayout;  // reuse lightMVP push constant
    pipelineInfo.renderPass          = m_shadowRenderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowObjectPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow object pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
}

// ============================================================
//  Shadow pipeline for the player cube (instanced position)
// ============================================================
void VulkanContext::createShadowPlayerPipeline() {
    auto vert = readFile("shaders/shadow_player.vert.spv");
    VkShaderModule vertMod = createShaderModule(vert);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertMod;
    vertStage.pName  = "main";

    // Cube mesh (Vertex pos) + per-instance position (InstanceData)
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(Vertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(InstanceData);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)       };
    attrs[1] = { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, pos) };

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 2;
    vertInput.pVertexBindingDescriptions      = bindings;
    vertInput.vertexAttributeDescriptionCount = 2;
    vertInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0.0f, 1.0f};
    VkRect2D   scissor{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode                = VK_CULL_MODE_NONE; // match chunk shadow (tighter contact)
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasSlopeFactor    = 0.0f;
    rasterizer.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 1;
    pipelineInfo.pStages             = &vertStage;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.layout              = m_shadowPipelineLayout;  // reuse lightMVP push constant
    pipelineInfo.renderPass          = m_shadowRenderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPlayerPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow player pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
}

// ============================================================
//  Shadow pipeline for alpha-tested grass cards
// ============================================================
void VulkanContext::createShadowGrassPipeline() {
    VkDescriptorSetLayoutBinding opacityBinding{};
    opacityBinding.binding         = 0;
    opacityBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    opacityBinding.descriptorCount = 1;
    opacityBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings    = &opacityBinding;
    if (vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &m_shadowGrassDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow grass descriptor set layout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &m_shadowGrassDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_shadowGrassPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow grass pipeline layout");

    auto vert = readFile("shaders/shadow_grass.vert.spv");
    auto frag = readFile("shaders/shadow_grass.frag.spv");
    VkShaderModule vertMod = createShaderModule(vert);
    VkShaderModule fragMod = createShaderModule(frag);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(GrassCardVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(ObjectInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassCardVertex, pos) };
    attrs[1] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GrassCardVertex, uv)  };
    attrs[2] = { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ObjectInstance, pos)   };
    attrs[3] = { 4, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, scale) };
    attrs[4] = { 5, 1, VK_FORMAT_R32_SFLOAT,       offsetof(ObjectInstance, rot)   };

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 2;
    vertInput.pVertexBindingDescriptions      = bindings;
    vertInput.vertexAttributeDescriptionCount = 5;
    vertInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0.0f, 1.0f};
    VkRect2D   scissor{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode                = VK_CULL_MODE_NONE;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 0.2f;
    rasterizer.depthBiasSlopeFactor    = 0.4f;
    rasterizer.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.layout              = m_shadowGrassPipelineLayout;
    pipelineInfo.renderPass          = m_shadowRenderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowGrassPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow grass pipeline");

    vkDestroyShaderModule(m_device, fragMod, nullptr);
    vkDestroyShaderModule(m_device, vertMod, nullptr);
}

void VulkanContext::createShadowGrassDescriptors() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = MAX_FRAMES_IN_FLIGHT;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_shadowGrassDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow grass descriptor pool");

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_shadowGrassDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_shadowGrassDescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();

    m_shadowGrassDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_shadowGrassDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate shadow grass descriptor sets");

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo opacityInfo{};
        opacityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        opacityInfo.imageView   = m_grassOpacityTex.view;
        opacityInfo.sampler     = m_grassOpacityTex.sampler;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_shadowGrassDescriptorSets[i];
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &opacityInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

// ============================================================
//  Shadow sampler
// ============================================================
void VulkanContext::createShadowSampler() {
    VkSamplerCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter     = VK_FILTER_LINEAR;
    info.minFilter     = VK_FILTER_LINEAR;
    info.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // depth=1.0 outside map = lit
    info.compareEnable = VK_TRUE;
    info.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(m_device, &info, nullptr, &m_shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow sampler");
}

// ============================================================
//  Descriptor set layout
// ============================================================
void VulkanContext::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[5]{};

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding         = 3; // terrain texture array (sampler2DArray)
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding         = 4; // grass opacity mask
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 5;
    info.pBindings    = bindings;

    if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");
}

// ============================================================
//  Uniform buffers
// ============================================================
void VulkanContext::createUniformBuffers() {
    VkDeviceSize size = sizeof(UniformBufferObject);
    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_uniformBuffers[i] = createBuffer(size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_uniformBuffers[i].memory, 0, size, 0, &m_uniformBuffers[i].mapped);
    }
}

void VulkanContext::createReflectionUniformBuffers() {
    VkDeviceSize size = sizeof(UniformBufferObject);
    m_reflectionUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_reflectionUniformBuffers[i] = createBuffer(size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_reflectionUniformBuffers[i].memory, 0, size, 0, &m_reflectionUniformBuffers[i].mapped);
    }
}

// ============================================================
//  Descriptor pool + sets
// ============================================================
void VulkanContext::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 8; // main + reflection scene descriptors

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 2;
    info.pPoolSizes    = poolSizes;
    info.maxSets       = MAX_FRAMES_IN_FLIGHT * 2;

    if (vkCreateDescriptorPool(m_device, &info, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}

void VulkanContext::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor sets");

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView   = m_shadowImageView;
        imageInfo.sampler     = m_shadowSampler;

        VkDescriptorImageInfo grassInfo{};
        grassInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        grassInfo.imageView   = m_grassTex.view;
        grassInfo.sampler     = m_grassTex.sampler;

        VkDescriptorImageInfo grassOpacityInfo{};
        grassOpacityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        grassOpacityInfo.imageView   = m_grassOpacityTex.view;
        grassOpacityInfo.sampler     = m_grassOpacityTex.sampler;

        VkDescriptorImageInfo terrainInfo{};
        terrainInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        terrainInfo.imageView   = m_terrainTex.view;
        terrainInfo.sampler     = m_terrainTex.sampler;

        VkWriteDescriptorSet writes[5]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_descriptorSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &bufferInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_descriptorSets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &imageInfo;

        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_descriptorSets[i];
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &grassInfo;

        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_descriptorSets[i];
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &terrainInfo;

        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_descriptorSets[i];
        writes[4].dstBinding      = 4;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo      = &grassOpacityInfo;

        vkUpdateDescriptorSets(m_device, 5, writes, 0, nullptr);
    }
}

void VulkanContext::createReflectionDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();

    m_reflectionDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_reflectionDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate reflection descriptor sets");

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_reflectionUniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView   = m_shadowImageView;
        imageInfo.sampler     = m_shadowSampler;

        VkDescriptorImageInfo grassInfo{};
        grassInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        grassInfo.imageView   = m_grassTex.view;
        grassInfo.sampler     = m_grassTex.sampler;

        VkDescriptorImageInfo grassOpacityInfo{};
        grassOpacityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        grassOpacityInfo.imageView   = m_grassOpacityTex.view;
        grassOpacityInfo.sampler     = m_grassOpacityTex.sampler;

        VkDescriptorImageInfo terrainInfo{};
        terrainInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        terrainInfo.imageView   = m_terrainTex.view;
        terrainInfo.sampler     = m_terrainTex.sampler;

        VkWriteDescriptorSet writes[5]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_reflectionDescriptorSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &bufferInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_reflectionDescriptorSets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &imageInfo;

        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_reflectionDescriptorSets[i];
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &grassInfo;

        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_reflectionDescriptorSets[i];
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &terrainInfo;

        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_reflectionDescriptorSets[i];
        writes[4].dstBinding      = 4;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo      = &grassOpacityInfo;

        vkUpdateDescriptorSets(m_device, 5, writes, 0, nullptr);
    }
}

void VulkanContext::createOceanDescriptors() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 4; // reflection + 2 normal maps + displacement

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = MAX_FRAMES_IN_FLIGHT;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_oceanDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean descriptor pool");

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_oceanDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_oceanDescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();

    m_oceanDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_oceanDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate ocean descriptor sets");

    updateOceanDescriptors();
}

void VulkanContext::updateOceanDescriptors() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(UniformBufferObject);

        VkDescriptorImageInfo reflectionInfo{};
        reflectionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        reflectionInfo.imageView   = m_reflectionView[i];
        reflectionInfo.sampler     = m_postSampler;

        VkDescriptorImageInfo normalAInfo{};
        normalAInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalAInfo.imageView   = m_oceanNormalA.view;
        normalAInfo.sampler     = m_oceanNormalA.sampler;

        VkDescriptorImageInfo normalBInfo{};
        normalBInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalBInfo.imageView   = m_oceanNormalB.view;
        normalBInfo.sampler     = m_oceanNormalB.sampler;

        VkDescriptorImageInfo displacementInfo{};
        displacementInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // storage image, kept in GENERAL
        displacementInfo.imageView   = m_oceanDisplacementView;
        displacementInfo.sampler     = m_oceanDisplacementSampler;

        VkWriteDescriptorSet writes[5]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_oceanDescriptorSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &bufferInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_oceanDescriptorSets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &reflectionInfo;

        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_oceanDescriptorSets[i];
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &normalAInfo;

        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_oceanDescriptorSets[i];
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &normalBInfo;

        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_oceanDescriptorSets[i];
        writes[4].dstBinding      = 4;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo      = &displacementInfo;

        vkUpdateDescriptorSets(m_device, 5, writes, 0, nullptr);
    }
}

// ============================================================
//  Static geometry buffers
// ============================================================
void VulkanContext::createIndexBuffer() {
    VkDeviceSize size = sizeof(kIndices[0]) * kIndices.size();

    GpuBuffer staging = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &data);
    memcpy(data, kIndices.data(), (size_t)size);
    vkUnmapMemory(m_device, staging.memory);

    m_indexBuffer = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(staging, m_indexBuffer, size);
    // staging frees itself at scope exit
}

void VulkanContext::createVertexBuffer() {
    VkDeviceSize size = sizeof(kVertices[0]) * kVertices.size();

    GpuBuffer staging = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &data);
    memcpy(data, kVertices.data(), (size_t)size);
    vkUnmapMemory(m_device, staging.memory);

    m_vertexBuffer = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(staging, m_vertexBuffer, size);
    // staging frees itself at scope exit
}

void VulkanContext::createItemMesh() {
    // Small cube: reuse the unit cube scaled down so dropped items read as little pickups.
    std::vector<Vertex> verts = kVertices;
    for (auto& v : verts) v.pos *= 0.3f;
    VkDeviceSize size = sizeof(Vertex) * verts.size();

    GpuBuffer staging = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &data);
    memcpy(data, verts.data(), (size_t)size);
    vkUnmapMemory(m_device, staging.memory);

    m_itemVertexBuffer = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(staging, m_itemVertexBuffer, size);
    // staging frees itself at scope exit
}

void VulkanContext::createDropInstanceBuffer() {
    VkDeviceSize size = sizeof(InstanceData) * MAX_DROPS;
    m_dropInstBuffer.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_dropInstBuffer[i] = createBuffer(size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_dropInstBuffer[i].memory, 0, size, 0, &m_dropInstBuffer[i].mapped);
    }
}

void VulkanContext::createPlayerInstanceBuffer(const glm::vec3& playerPosition) {
    VkDeviceSize size = sizeof(InstanceData);
    m_playerInstBuffer.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_playerInstBuffer[i] = createBuffer(size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_playerInstBuffer[i].memory, 0, size, 0, &m_playerInstBuffer[i].mapped);
    }

    updatePlayerInstanceBuffer(playerPosition);
}

void VulkanContext::createSelectorBuffers() {
    VkDeviceSize vertexSize = sizeof(kSelectorVertices[0]) * kSelectorVertices.size();

    GpuBuffer staging = createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* data;
    vkMapMemory(m_device, staging.memory, 0, vertexSize, 0, &data);
    memcpy(data, kSelectorVertices.data(), vertexSize);
    vkUnmapMemory(m_device, staging.memory);

    m_selectorVertexBuffer = createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    copyBuffer(staging, m_selectorVertexBuffer, vertexSize);

    VkDeviceSize indexSize = sizeof(kSelectorIndices[0]) * kSelectorIndices.size();
    staging = createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkMapMemory(m_device, staging.memory, 0, indexSize, 0, &data);
    memcpy(data, kSelectorIndices.data(), indexSize);
    vkUnmapMemory(m_device, staging.memory);

    m_selectorIndexBuffer = createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    copyBuffer(staging, m_selectorIndexBuffer, indexSize);
    // staging frees itself at scope exit

    VkDeviceSize instanceSize = sizeof(InstanceData);
    m_selectorInstBuffer.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_selectorInstBuffer[i] = createBuffer(instanceSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_selectorInstBuffer[i].memory, 0, instanceSize, 0, &m_selectorInstBuffer[i].mapped);
    }
}
