#include "VulkanContext.h"
#include "VulkanContext_Private.h"
#include "renderer/Types.h"
#include "platform/Window.h"
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
#include <sstream>
#include <initializer_list>

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

uint32_t readLe32(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 4 > bytes.size())
        throw std::runtime_error("Unexpected end of binary file");
    return (uint32_t)bytes[offset]
         | ((uint32_t)bytes[offset + 1] << 8)
         | ((uint32_t)bytes[offset + 2] << 16)
         | ((uint32_t)bytes[offset + 3] << 24);
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open binary file: " + path);
    const size_t size = (size_t)file.tellg();
    std::vector<uint8_t> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)size);
    return bytes;
}

glm::vec3 importShipPosition(glm::vec3 p) {
    return glm::vec3(-p.z, p.x, p.y);
}

glm::vec3 importShipDirection(glm::vec3 v) {
    glm::vec3 mapped(-v.z, v.x, v.y);
    if (glm::length(mapped) <= 0.00001f)
        return glm::vec3(0.0f, 0.0f, 1.0f);
    return glm::normalize(mapped);
}

struct ObjIndex {
    int v = -1;
    int vt = -1;
    int vn = -1;
};

ObjIndex parseObjIndex(const std::string& token) {
    ObjIndex idx;
    size_t first = token.find('/');
    size_t second = first == std::string::npos ? std::string::npos : token.find('/', first + 1);

    idx.v = std::stoi(token.substr(0, first)) - 1;
    if (first != std::string::npos && second > first + 1)
        idx.vt = std::stoi(token.substr(first + 1, second - first - 1)) - 1;
    if (second != std::string::npos && second + 1 < token.size())
        idx.vn = std::stoi(token.substr(second + 1)) - 1;
    return idx;
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

// True if the device exposes every extension in kDeviceExtensions (currently VK_KHR_swapchain).
bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(kDeviceExtensions.begin(), kDeviceExtensions.end());
    for (const auto& ext : available)
        required.erase(ext.extensionName);
    return required.empty();
}

// True if the surface exposes at least one format and present mode. Call only after the
// swapchain extension is confirmed (querying surface support otherwise is undefined).
bool VulkanContext::isSwapchainAdequate(VkPhysicalDevice device) {
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &modeCount, nullptr);
    return formatCount > 0 && modeCount > 0;
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Suitable = required queues + swapchain extension + a usable surface format/present mode.
    // Order matters: confirm the extension before querying surface support.
    auto isSuitable = [&](VkPhysicalDevice device) {
        return findQueueFamilies(device).complete()
            && checkDeviceExtensionSupport(device)
            && isSwapchainAdequate(device);
    };

    // Prefer discrete GPU; otherwise take first suitable device
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (auto device : devices) {
        if (!isSuitable(device)) continue;
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
    if (!supported.shaderClipDistance)
        throw std::runtime_error("shaderClipDistance is required for planar water reflection clipping");
    features.shaderClipDistance = VK_TRUE;
    if (!supported.textureCompressionBC)
        throw std::runtime_error("BC texture compression is required for imported ship DDS materials");
    features.textureCompressionBC = VK_TRUE;
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
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
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
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Scene color/depth writes must be visible to either the continuation pass or post sampling.
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

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

    // Continuation pass over the same scene targets. The first scene pass clears and records
    // opaque terrain/object depth; this pass loads those attachments so water and late dynamic
    // entities can be separated cleanly before sampled-depth refraction/thickness is added.
    color.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription loadAttachments[] = {color, depth};
    info.pAttachments = loadAttachments;
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_sceneLoadRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create scene continuation render pass");
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
    depthStencil.depthWriteEnable = (cfg.depthTest && cfg.depthWrite) ? VK_TRUE : VK_FALSE;
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

void VulkanContext::createScenePipelineLayout() {
    // Shared scene pipeline layout (UBO + shadow sampler descriptor), reused by the
    // sky / chunk / object / grass / ship draw pipelines. The legacy instanced-cube
    // pipeline that originally lived here was removed with the farm player cube / drops.
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_descriptorSetLayout;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");
}

void VulkanContext::createSkyPipeline() {
    PipelineConfig cfg;
    cfg.vertPath   = "shaders/post.vert.spv";
    cfg.fragPath   = "shaders/sky.frag.spv";
    cfg.bindings   = {};
    cfg.attributes = {};
    cfg.cullMode   = VK_CULL_MODE_NONE;
    cfg.depthTest  = false;
    cfg.alphaBlend = false;
    cfg.layout     = m_pipelineLayout; // reuse the scene UBO descriptor set
    m_skyPipeline = createPipeline(cfg);
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

void VulkanContext::createOceanDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[11]{};

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

    // FFT displacement map — sampled by the vertex shader (mesh displacement) and the
    // fragment shader (per-fragment wave normal).
    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Ship wake mask — vertex samples height, fragment samples foam/turbulence.
    bindings[5].binding         = 5;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Per-cascade surface-slope map — fragment builds smooth wave normals from it.
    bindings[6].binding         = 6;
    bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Pre-water scene depth copy — sampled by water for thickness/refraction decisions.
    bindings[7].binding         = 7;
    bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Pre-water scene color copy — sampled by water for screen-space refraction.
    bindings[8].binding         = 8;
    bindings[8].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Previous frame's resolved scene color - sampled by water for temporal SSR stability.
    bindings[9].binding         = 9;
    bindings[9].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Previous frame's pre-water scene depth - rejects temporal SSR history after disocclusion.
    bindings[10].binding         = 10;
    bindings[10].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 11;
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
    cfg.depthWrite = true; // Current scene order draws the ship after water, so water must keep depth occlusion.
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

    // Dense near/mid rings so the tall FFT waves are not faceted into visible triangles at the
    // grazing sailing view; spacing grows gradually toward the horizon where detail is sub-pixel.
    appendRings(  64.0f,  0.5f);
    appendRings( 256.0f,  1.0f);
    appendRings( 640.0f,  4.0f);
    appendRings(1408.0f,  8.0f);
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
    vkCheck(vkMapMemory(m_device, m_oceanVertexBuffer.memory, 0, vSize, 0, &vMapped),
        "Failed to map ocean vertex buffer");
    memcpy(vMapped, verts.data(), vSize);
    vkUnmapMemory(m_device, m_oceanVertexBuffer.memory);

    VkDeviceSize iSize = sizeof(uint32_t) * indices.size();
    m_oceanIndexBuffer = createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* iMapped;
    vkCheck(vkMapMemory(m_device, m_oceanIndexBuffer.memory, 0, iSize, 0, &iMapped),
        "Failed to map ocean index buffer");
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
    cfg.fragPath   = "shaders/ship.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(ShipVertex), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ShipVertex, pos)     },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ShipVertex, normal)  },
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ShipVertex, tangent) },
        { 3, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ShipVertex, uv)      },
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;  // procedural hull mesh — winding not guaranteed
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_shipPipelineLayout;
    m_shipPipeline = createPipeline(cfg);
}

void VulkanContext::createPortPipeline() {
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
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_portPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create port pipeline layout");

    PipelineConfig cfg;
    cfg.vertPath   = "shaders/port.vert.spv";
    cfg.fragPath   = "shaders/port.frag.spv";
    cfg.bindings   = {
        { 0, sizeof(PortVertex), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    cfg.attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(PortVertex, pos)    },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(PortVertex, normal) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(PortVertex, color)  },
    };
    cfg.cullMode   = VK_CULL_MODE_NONE;
    cfg.depthTest  = true;
    cfg.alphaBlend = false;
    cfg.layout     = m_portPipelineLayout;
    m_portPipeline = createPipeline(cfg);

    auto vert = readFile("shaders/shadow.vert.spv");
    VkShaderModule vertMod = createShaderModule(vert);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertMod;
    vertStage.pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(PortVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.binding  = 0;
    attr.location = 0;
    attr.format   = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset   = offsetof(PortVertex, pos);

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
    rasterizer.cullMode                = VK_CULL_MODE_NONE;
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
    pipelineInfo.layout              = m_shadowPipelineLayout;
    pipelineInfo.renderPass          = m_shadowRenderPass;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_portShadowPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create port shadow pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
}

void VulkanContext::createPortMesh() {
    std::vector<PortVertex> verts;
    verts.reserve(4096);

    auto pushTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        verts.push_back({a, n, color});
        verts.push_back({b, n, color});
        verts.push_back({c, n, color});
    };
    auto pushQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec4 color) {
        pushTri(a, b, c, color);
        pushTri(a, c, d, color);
    };
    auto addBox = [&](glm::vec3 mn, glm::vec3 mx, glm::vec4 color) {
        glm::vec3 p000{mn.x, mn.y, mn.z};
        glm::vec3 p100{mx.x, mn.y, mn.z};
        glm::vec3 p110{mx.x, mx.y, mn.z};
        glm::vec3 p010{mn.x, mx.y, mn.z};
        glm::vec3 p001{mn.x, mn.y, mx.z};
        glm::vec3 p101{mx.x, mn.y, mx.z};
        glm::vec3 p111{mx.x, mx.y, mx.z};
        glm::vec3 p011{mn.x, mx.y, mx.z};
        pushQuad(p001, p101, p111, p011, color); // top
        pushQuad(p100, p000, p010, p110, color); // bottom
        pushQuad(p000, p001, p011, p010, color); // -x
        pushQuad(p101, p100, p110, p111, color); // +x
        pushQuad(p000, p100, p101, p001, color); // -y
        pushQuad(p110, p010, p011, p111, color); // +y
    };
    auto addGableRoof = [&](float x0, float x1, float y0, float y1, float z0, float ridgeZ, glm::vec4 color) {
        glm::vec3 a{x0, y0, z0};
        glm::vec3 b{x1, y0, z0};
        glm::vec3 c{x1, y1, z0};
        glm::vec3 d{x0, y1, z0};
        glm::vec3 r0{(x0 + x1) * 0.5f, y0, ridgeZ};
        glm::vec3 r1{(x0 + x1) * 0.5f, y1, ridgeZ};
        pushTri(a, b, r0, color);
        pushTri(d, r1, c, color);
        pushQuad(a, r0, r1, d, color);
        pushQuad(b, c, r1, r0, color);
    };
    auto addCylinder = [&](glm::vec2 center, float radius, float z0, float z1, uint32_t segments, glm::vec4 color) {
        for (uint32_t i = 0; i < segments; i++) {
            float a0 = 6.28318530f * (float)i / (float)segments;
            float a1 = 6.28318530f * (float)(i + 1) / (float)segments;
            glm::vec3 p0{center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, z0};
            glm::vec3 p1{center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, z0};
            glm::vec3 p2{center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, z1};
            glm::vec3 p3{center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, z1};
            pushQuad(p0, p1, p2, p3, color);
        }
    };
    auto addCone = [&](glm::vec2 center, float radius, float z0, float z1, uint32_t segments, glm::vec4 color) {
        glm::vec3 tip{center.x, center.y, z1};
        for (uint32_t i = 0; i < segments; i++) {
            float a0 = 6.28318530f * (float)i / (float)segments;
            float a1 = 6.28318530f * (float)(i + 1) / (float)segments;
            glm::vec3 p0{center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, z0};
            glm::vec3 p1{center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, z0};
            pushTri(p0, p1, tip, color);
        }
    };

    const glm::vec4 dockWood{0.36f, 0.23f, 0.13f, 0.0f};
    const glm::vec4 darkWood{0.20f, 0.14f, 0.09f, 0.0f};
    const glm::vec4 brick{0.42f, 0.19f, 0.12f, 0.0f};
    const glm::vec4 stone{0.52f, 0.49f, 0.40f, 0.0f};
    const glm::vec4 roof{0.075f, 0.085f, 0.090f, 0.0f};
    const glm::vec4 plaster{0.70f, 0.66f, 0.52f, 0.0f};
    const glm::vec4 beacon{1.0f, 0.72f, 0.34f, 4.5f};

    // Dock deck and an offshore pier, both above the rest sea level.
    addBox({-30.0f, -7.0f, 0.50f}, { 30.0f,  7.0f, 1.15f}, dockWood);
    addBox({ -5.2f,-46.0f, 0.52f}, {  5.2f, -4.0f, 1.12f}, dockWood);
    addBox({-34.0f,  7.0f, 0.55f}, { 34.0f, 10.0f, 1.25f}, darkWood);

    for (float x : {-24.0f, -12.0f, 0.0f, 12.0f, 24.0f}) {
        for (float y : {-41.0f, -25.0f, -9.0f, 4.0f}) {
            addBox({x - 0.55f, y - 0.55f, -2.2f}, {x + 0.55f, y + 0.55f, 0.75f}, darkWood);
        }
    }

    // Warehouses give the port a readable industrial-era silhouette.
    addBox({-28.0f, 12.0f, 0.50f}, {-8.0f, 28.0f, 8.6f}, brick);
    addGableRoof(-29.5f, -6.5f, 10.5f, 29.5f, 8.6f, 12.3f, roof);
    addBox({  8.0f, 13.0f, 0.50f}, {27.0f, 26.0f, 7.2f}, stone);
    addGableRoof(6.5f, 28.5f, 11.5f, 27.5f, 7.2f, 10.4f, roof);

    // Lighthouse tower, lantern room, and roof.
    addCylinder({38.0f, 8.0f}, 3.4f, 0.50f, 19.0f, 18, plaster);
    addCylinder({38.0f, 8.0f}, 4.2f, 18.2f, 20.1f, 18, stone);
    addCylinder({38.0f, 8.0f}, 3.4f, 20.1f, 22.5f, 18, beacon);
    addBox({34.2f, 7.1f, 20.7f}, {41.8f, 8.9f, 22.0f}, beacon);
    addBox({37.1f, 4.2f, 20.7f}, {38.9f, 11.8f, 22.0f}, beacon);
    addCone({38.0f, 8.0f}, 4.6f, 22.2f, 25.4f, 18, roof);

    m_portMesh.count = (uint32_t)verts.size();
    VkDeviceSize size = sizeof(PortVertex) * verts.size();
    m_portMesh.vbuf = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    vkCheck(vkMapMemory(m_device, m_portMesh.vbuf.memory, 0, size, 0, &mapped),
        "Failed to map port vertex buffer");
    memcpy(mapped, verts.data(), size);
    vkUnmapMemory(m_device, m_portMesh.vbuf.memory);
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
        vkCheck(vkMapMemory(m_device, m_uiBuffer[i].memory, 0, size, 0, &m_uiBuffer[i].mapped),
            "Failed to map UI vertex buffer");
    }
}

void VulkanContext::loadImportedShipMesh() {
    const std::string base = "assets/models/ships/lsv018/";
    const std::string objPath = base + "LSV018.obj";

    m_shipAlbedoTex   = createDDSBC1Texture(base + "LSV018_a.dds", true);
    m_shipNormalTex   = createDDSBC1Texture(base + "LSV018_n.dds", false);
    m_shipSpecularTex = createDDSBC1Texture(base + "LSV018_s.dds", false);

    std::ifstream file(objPath);
    if (!file.is_open())
        throw std::runtime_error("Cannot open ship OBJ: " + objPath);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
    std::vector<ShipVertex> verts;
    verts.reserve(90000);

    auto makeVertex = [&](const ObjIndex& idx) {
        if (idx.v < 0 || idx.v >= (int)positions.size())
            throw std::runtime_error("Ship OBJ face references an invalid position");

        ShipVertex v{};
        v.pos = importShipPosition(positions[(size_t)idx.v]);
        if (idx.vn >= 0 && idx.vn < (int)normals.size())
            v.normal = importShipDirection(normals[(size_t)idx.vn]);
        else
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);

        if (idx.vt >= 0 && idx.vt < (int)texcoords.size()) {
            glm::vec2 uv = texcoords[(size_t)idx.vt];
            v.uv = glm::vec2(uv.x, 1.0f - uv.y);
        } else {
            v.uv = glm::vec2(0.0f);
        }
        return v;
    };

    auto appendTriangle = [&](ShipVertex a, ShipVertex b, ShipVertex c) {
        glm::vec3 edge1 = b.pos - a.pos;
        glm::vec3 edge2 = c.pos - a.pos;
        glm::vec2 duv1 = b.uv - a.uv;
        glm::vec2 duv2 = c.uv - a.uv;
        float det = duv1.x * duv2.y - duv1.y * duv2.x;
        glm::vec3 tangent = glm::length(edge1) > 0.00001f ? glm::normalize(edge1) : glm::vec3(1.0f, 0.0f, 0.0f);
        if (std::abs(det) > 0.000001f) {
            tangent = glm::normalize((edge1 * duv2.y - edge2 * duv1.y) / det);
        }

        glm::vec3 faceNormal = glm::cross(edge1, edge2);
        if (glm::length(faceNormal) > 0.00001f)
            faceNormal = glm::normalize(faceNormal);
        else
            faceNormal = glm::vec3(0.0f, 0.0f, 1.0f);

        ShipVertex tri[3] = {a, b, c};
        for (ShipVertex& v : tri) {
            if (glm::length(v.normal) < 0.00001f)
                v.normal = faceNormal;
            v.tangent = tangent - v.normal * glm::dot(v.normal, tangent);
            if (glm::length(v.tangent) > 0.00001f)
                v.tangent = glm::normalize(v.tangent);
            else
                v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            verts.push_back(v);
        }
    };

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "v") {
            glm::vec3 p;
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        } else if (tag == "vn") {
            glm::vec3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "f") {
            std::vector<ObjIndex> face;
            std::string token;
            while (iss >> token)
                face.push_back(parseObjIndex(token));
            for (size_t i = 1; i + 1 < face.size(); ++i)
                appendTriangle(makeVertex(face[0]), makeVertex(face[i]), makeVertex(face[i + 1]));
        }
    }

    if (verts.empty())
        throw std::runtime_error("Ship OBJ produced no triangles: " + objPath);

    glm::vec3 localMin = verts[0].pos;
    glm::vec3 localMax = verts[0].pos;
    std::vector<float> zValues;
    zValues.reserve(verts.size());
    for (const ShipVertex& v : verts) {
        localMin = glm::min(localMin, v.pos);
        localMax = glm::max(localMax, v.pos);
        zValues.push_back(v.pos.z);
    }

    const size_t zCutIndex = std::min(zValues.size() - 1, (size_t)((float)zValues.size() * 0.35f));
    std::nth_element(zValues.begin(), zValues.begin() + zCutIndex, zValues.end());
    const float footprintZMax = zValues[zCutIndex];

    glm::vec3 footprintMin{0.0f};
    glm::vec3 footprintMax{0.0f};
    uint32_t footprintCount = 0;
    auto includeFootprintVertex = [&](const glm::vec3& p) {
        if (footprintCount == 0) {
            footprintMin = p;
            footprintMax = p;
        } else {
            footprintMin = glm::min(footprintMin, p);
            footprintMax = glm::max(footprintMax, p);
        }
        ++footprintCount;
    };

    for (const ShipVertex& v : verts) {
        if (v.pos.z <= footprintZMax)
            includeFootprintVertex(v.pos);
    }
    if (footprintCount < 128) {
        footprintCount = 0;
        for (const ShipVertex& v : verts)
            includeFootprintVertex(v.pos);
    }

    const float lengthLocal = std::max(footprintMax.x - footprintMin.x, 0.001f);
    const float centerlineLocal = (footprintMin.y + footprintMax.y) * 0.5f;
    const float fallbackHalfBeam = std::max(
        std::abs(footprintMax.y - centerlineLocal),
        std::abs(footprintMin.y - centerlineLocal)) * SHIP_WORLD_SCALE;

    m_shipHullProfile.localBoundsMin = localMin;
    m_shipHullProfile.localBoundsMax = localMax;
    m_shipHullProfile.sternOffset = std::max(-footprintMin.x * SHIP_WORLD_SCALE, 0.5f);
    m_shipHullProfile.bowOffset = std::max(footprintMax.x * SHIP_WORLD_SCALE, 0.5f);
    m_shipHullProfile.centerlineOffset = centerlineLocal * SHIP_WORLD_SCALE;
    m_shipHullProfile.halfBeam = std::max(fallbackHalfBeam, 0.5f);
    m_shipHullProfile.waterlineCutZ = footprintZMax;
    m_shipHullProfile.halfWidthSamples.fill(0.0f);

    for (const ShipVertex& v : verts) {
        if (v.pos.z > footprintZMax)
            continue;
        const float t = std::clamp((v.pos.x - footprintMin.x) / lengthLocal, 0.0f, 1.0f);
        const uint32_t sample = (uint32_t)std::round(t * (float)(SHIP_HULL_PROFILE_SAMPLES - 1));
        const float halfWidth = std::abs(v.pos.y - centerlineLocal) * SHIP_WORLD_SCALE;
        m_shipHullProfile.halfWidthSamples[sample] =
            std::max(m_shipHullProfile.halfWidthSamples[sample], halfWidth);
    }

    for (uint32_t i = 0; i < SHIP_HULL_PROFILE_SAMPLES; ++i) {
        if (m_shipHullProfile.halfWidthSamples[i] > 0.0f)
            continue;

        int left = (int)i - 1;
        while (left >= 0 && m_shipHullProfile.halfWidthSamples[(size_t)left] <= 0.0f)
            --left;
        uint32_t right = i + 1;
        while (right < SHIP_HULL_PROFILE_SAMPLES && m_shipHullProfile.halfWidthSamples[right] <= 0.0f)
            ++right;

        if (left >= 0 && right < SHIP_HULL_PROFILE_SAMPLES) {
            const float span = (float)(right - (uint32_t)left);
            const float f = (float)(i - (uint32_t)left) / span;
            m_shipHullProfile.halfWidthSamples[i] =
                glm::mix(m_shipHullProfile.halfWidthSamples[(size_t)left],
                         m_shipHullProfile.halfWidthSamples[right], f);
        } else if (left >= 0) {
            m_shipHullProfile.halfWidthSamples[i] = m_shipHullProfile.halfWidthSamples[(size_t)left];
        } else if (right < SHIP_HULL_PROFILE_SAMPLES) {
            m_shipHullProfile.halfWidthSamples[i] = m_shipHullProfile.halfWidthSamples[right];
        } else {
            m_shipHullProfile.halfWidthSamples[i] = m_shipHullProfile.halfBeam;
        }
    }

    m_shipMesh.count = (uint32_t)verts.size();
    VkDeviceSize size = sizeof(ShipVertex) * verts.size();
    m_shipMesh.vbuf = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkCheck(vkMapMemory(m_device, m_shipMesh.vbuf.memory, 0, size, 0, &mapped),
        "Failed to map ship mesh buffer");
    memcpy(mapped, verts.data(), (size_t)size);
    vkUnmapMemory(m_device, m_shipMesh.vbuf.memory);
}

// (createObjectMeshes — the farm tree/rock/fence prop meshes — was removed with the
//  object dressing rendering. The hero ship mesh load that lived at its end is now
//  called directly from the constructor via loadImportedShipMesh().)

TextureResource VulkanContext::createTextureArray(uint32_t width, uint32_t height, uint32_t layerCount,
    VkFormat format, const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped)
{
    TextureResource tex;
    tex.device = m_device;

    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkCheck(vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped),
        "Failed to map texture array staging buffer");
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
    vkCheck(vkBindImageMemory(m_device, tex.image, tex.memory, 0),
        "Failed to bind texture array memory");

    // One-shot upload: transition all layers, copy the contiguous layer-major buffer, transition to read.
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandPool        = m_commandPool;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkCheck(vkAllocateCommandBuffers(m_device, &cbAlloc, &cmd),
        "Failed to allocate texture array upload command buffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &begin),
        "Failed to begin texture array upload command buffer");

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

    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end texture array upload command buffer");
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &fence),
        "Failed to create texture array upload fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submit, fence),
        "Failed to submit texture array upload command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for texture array upload fence");
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

    // Scene continuation framebuffers — same attachments, created against the load render pass.
    m_sceneLoadFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView attachments[] = { m_offscreenView[i], m_depthImageView };
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_sceneLoadRenderPass;
        info.attachmentCount = 2;
        info.pAttachments    = attachments;
        info.width           = m_swapchainExtent.width;
        info.height          = m_swapchainExtent.height;
        info.layers          = 1;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_sceneLoadFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene continuation framebuffer");
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

    // LDR tone-map target framebuffers (SMAA input; sRGB pass).
    m_ldrFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView attachments[] = { m_ldrView[i] };
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_postLdrRenderPass;
        info.attachmentCount = 1;
        info.pAttachments    = attachments;
        info.width           = m_swapchainExtent.width;
        info.height          = m_swapchainExtent.height;
        info.layers          = 1;
        if (vkCreateFramebuffer(m_device, &info, nullptr, &m_ldrFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post LDR framebuffer");
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

    // LDR tone-map target pass (SMAA input). Same shape, but sRGB like the
    // swapchain so the graded output keeps identical encoding, and DONT_CARE
    // load since the fullscreen post pass overwrites every pixel.
    color.format = VK_FORMAT_R8G8B8A8_SRGB;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_postLdrRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post LDR render pass");
}

void VulkanContext::createTaaRenderPass() {
    // Same shape as the SMAA pass but on the HDR scene format: fullscreen
    // overwrite, then sampled by the post pass (and as next frame's history).
    VkAttachmentDescription color{};
    color.format         = m_sceneColorFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
    // Wait on last frame's history read (and this frame's scene write) before overwriting.
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
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_taaRenderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA render pass");
}

void VulkanContext::createTaaResources() {
    m_taaHistoryFrames = 0;
    m_taaImage.resize(MAX_FRAMES_IN_FLIGHT);
    m_taaMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_taaView.resize(MAX_FRAMES_IN_FLIGHT);
    m_taaFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createImage(m_swapchainExtent.width, m_swapchainExtent.height, m_sceneColorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_taaImage[i], m_taaMemory[i]);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = m_taaImage[i];
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = m_sceneColorFormat;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_taaView[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create TAA image view");

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_taaRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &m_taaView[i];
        fbInfo.width           = m_swapchainExtent.width;
        fbInfo.height          = m_swapchainExtent.height;
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_taaFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create TAA framebuffer");

        // The other index is bound as history before this image is ever written;
        // move it out of UNDEFINED so the descriptor binding is always legal.
        transitionImageLayout(m_taaImage[i],
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void VulkanContext::createOffscreenResources() {
    m_temporalHistoryFrames = 0;
    m_prevViewProj = glm::mat4(1.0f);
    m_offscreenImage.resize(MAX_FRAMES_IN_FLIGHT);
    m_offscreenMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_offscreenView.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneColorCopyImage.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneColorCopyMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneColorCopyView.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneColorCopyReady.assign(MAX_FRAMES_IN_FLIGHT, false);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createImage(m_swapchainExtent.width, m_swapchainExtent.height, m_sceneColorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

        createImage(m_swapchainExtent.width, m_swapchainExtent.height, m_sceneColorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_sceneColorCopyImage[i], m_sceneColorCopyMemory[i]);

        v.image = m_sceneColorCopyImage[i];
        if (vkCreateImageView(m_device, &v, nullptr, &m_sceneColorCopyView[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene color copy image view");
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
                            VkFormat format,
                            const char* label)
    {
        images.resize(MAX_FRAMES_IN_FLIGHT);
        memories.resize(MAX_FRAMES_IN_FLIGHT);
        views.resize(MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            createImage(m_swapchainExtent.width, m_swapchainExtent.height, format,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                images[i], memories[i]);

            VkImageViewCreateInfo v{};
            v.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            v.image                           = images[i];
            v.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            v.format                          = format;
            v.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            v.subresourceRange.levelCount     = 1;
            v.subresourceRange.layerCount     = 1;
            if (vkCreateImageView(m_device, &v, nullptr, &views[i]) != VK_SUCCESS)
                throw std::runtime_error(std::string("Failed to create ") + label + " image view");
        }
    };

    createTarget(m_smaaEdgeImage, m_smaaEdgeMemory, m_smaaEdgeView, VK_FORMAT_R8G8B8A8_UNORM, "SMAA edge");
    createTarget(m_smaaBlendImage, m_smaaBlendMemory, m_smaaBlendView, VK_FORMAT_R8G8B8A8_UNORM, "SMAA blend");
    createTarget(m_ldrImage, m_ldrMemory, m_ldrView, VK_FORMAT_R8G8B8A8_SRGB, "post LDR");
}

TextureResource VulkanContext::createTexture(uint32_t width, uint32_t height, VkFormat format,
    const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped)
{
    TextureResource tex;
    tex.device = m_device;

    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkCheck(vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped),
        "Failed to map texture staging buffer");
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

TextureResource VulkanContext::createDDSBC1Texture(const std::string& path, bool srgb) {
    std::vector<uint8_t> bytes = readBinaryFile(path);
    if (bytes.size() < 128 || bytes[0] != 'D' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != ' ')
        throw std::runtime_error("Invalid DDS file: " + path);

    const uint32_t height = readLe32(bytes, 12);
    const uint32_t width  = readLe32(bytes, 16);
    const uint32_t mipCountRaw = readLe32(bytes, 28);
    const uint32_t fourCC = readLe32(bytes, 84);
    const uint32_t dxt1 = (uint32_t)'D' | ((uint32_t)'X' << 8) | ((uint32_t)'T' << 16) | ((uint32_t)'1' << 24);
    if (width == 0 || height == 0 || fourCC != dxt1)
        throw std::runtime_error("Only DXT1/BC1 DDS textures are supported for ship materials: " + path);

    const uint32_t mipLevels = std::max(1u, mipCountRaw);
    const VkDeviceSize payloadSize = (VkDeviceSize)(bytes.size() - 128);
    const uint8_t* payload = bytes.data() + 128;

    GpuBuffer staging = createBuffer(payloadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkCheck(vkMapMemory(m_device, staging.memory, 0, payloadSize, 0, &mapped),
        "Failed to map DDS texture staging buffer");
    memcpy(mapped, payload, (size_t)payloadSize);
    vkUnmapMemory(m_device, staging.memory);

    TextureResource tex;
    tex.device = m_device;
    const VkFormat format = srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory, mipLevels);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd),
        "Failed to allocate DDS upload command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &beginInfo),
        "Failed to begin DDS upload command buffer");

    VkImageMemoryBarrier toDst{};
    toDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image               = tex.image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = mipLevels;
    toDst.subresourceRange.layerCount = 1;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    std::vector<VkBufferImageCopy> copies;
    copies.reserve(mipLevels);
    VkDeviceSize offset = 0;
    uint32_t mipW = width;
    uint32_t mipH = height;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        const VkDeviceSize mipSize = (VkDeviceSize)std::max(1u, (mipW + 3u) / 4u)
                                   * (VkDeviceSize)std::max(1u, (mipH + 3u) / 4u) * 8u;
        if (offset + mipSize > payloadSize)
            throw std::runtime_error("DDS mip chain is shorter than declared: " + path);

        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {mipW, mipH, 1};
        copies.push_back(region);

        offset += mipSize;
        mipW = std::max(1u, mipW / 2u);
        mipH = std::max(1u, mipH / 2u);
    }

    vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)copies.size(), copies.data());

    VkImageMemoryBarrier toRead{};
    toRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image               = tex.image;
    toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toRead.subresourceRange.levelCount = mipLevels;
    toRead.subresourceRange.layerCount = 1;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end DDS upload command buffer");

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &fence),
        "Failed to create DDS upload fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submit, fence),
        "Failed to submit DDS upload command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for DDS upload fence");
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = tex.image;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = mipLevels;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &vi, nullptr, &tex.view) != VK_SUCCESS)
        throw std::runtime_error("Failed to create DDS texture view: " + path);

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
        throw std::runtime_error("Failed to create DDS texture sampler: " + path);

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

    // Same post shader targeting the LDR intermediate (SMAA input) instead of
    // the swapchain — only the render pass differs.
    pipelineInfo.renderPass = m_postLdrRenderPass;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_postLdrPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post LDR pipeline");

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

void VulkanContext::createTaaPipeline() {
    // Set layout: 0 = current scene color, 1 = history color, 2 = scene depth.
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t b = 0; b < 3; b++) {
        bindings[b].binding         = b;
        bindings[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dl{};
    dl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 3;
    dl.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &dl, nullptr, &m_taaDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA descriptor set layout");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset     = 0;
    push.size       = sizeof(TaaPushConstants);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount         = 1;
    pl.pSetLayouts            = &m_taaDescriptorSetLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &push;
    if (vkCreatePipelineLayout(m_device, &pl, nullptr, &m_taaPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA pipeline layout");

    auto vertCode = readFile("shaders/post.vert.spv");
    auto fragCode = readFile("shaders/taa.frag.spv");
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
    pipelineInfo.layout              = m_taaPipelineLayout;
    pipelineInfo.renderPass          = m_taaRenderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_taaPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA pipeline");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
}

void VulkanContext::updateTaaDescriptors() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Resolve inputs: this frame's scene, the other index as history
        // (frames in flight alternate, so [1-i] holds last frame's resolve).
        VkDescriptorImageInfo images[3]{};
        images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[0].imageView   = m_offscreenView[i];
        images[0].sampler     = m_postSampler;
        images[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[1].imageView   = m_taaView[(i + 1) % MAX_FRAMES_IN_FLIGHT];
        images[1].sampler     = m_postSampler;
        images[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[2].imageView   = m_depthImageView;
        images[2].sampler     = m_sceneDepthSampler;

        VkWriteDescriptorSet writes[3]{};
        for (uint32_t b = 0; b < 3; b++) {
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = m_taaDescriptorSets[i];
            writes[b].dstBinding      = b;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &images[b];
        }
        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);

        // Post pass variant that tone-maps the TAA resolve instead of the raw scene.
        VkDescriptorImageInfo resolved{};
        resolved.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        resolved.imageView   = m_taaView[i];
        resolved.sampler     = m_postSampler;
        VkWriteDescriptorSet postWrite{};
        postWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        postWrite.dstSet          = m_postTaaDescriptorSets[i];
        postWrite.dstBinding      = 0;
        postWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postWrite.descriptorCount = 1;
        postWrite.pImageInfo      = &resolved;
        vkUpdateDescriptorSets(m_device, 1, &postWrite, 0, nullptr);
    }
}

void VulkanContext::createTaaDescriptors() {
    VkDescriptorPoolSize ps{};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = MAX_FRAMES_IN_FLIGHT * 4; // 3 resolve inputs + 1 post input
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &ps;
    pi.maxSets       = MAX_FRAMES_IN_FLIGHT * 2;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_taaDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA descriptor pool");

    std::vector<VkDescriptorSetLayout> taaLayouts(MAX_FRAMES_IN_FLIGHT, m_taaDescriptorSetLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_taaDescriptorPool;
    ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    ai.pSetLayouts        = taaLayouts.data();
    m_taaDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &ai, m_taaDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate TAA descriptor sets");

    std::vector<VkDescriptorSetLayout> postLayouts(MAX_FRAMES_IN_FLIGHT, m_postDescriptorSetLayout);
    ai.pSetLayouts = postLayouts.data();
    m_postTaaDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &ai, m_postTaaDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate TAA post descriptor sets");

    updateTaaDescriptors();
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

    VkSamplerCreateInfo depthInfo = info;
    depthInfo.magFilter = VK_FILTER_NEAREST;
    depthInfo.minFilter = VK_FILTER_NEAREST;
    depthInfo.compareEnable = VK_FALSE;
    if (vkCreateSampler(m_device, &depthInfo, nullptr, &m_sceneDepthSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create scene depth sampler");
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
        // SMAA reads the tone-mapped/graded LDR target, not the HDR scene.
        VkDescriptorImageInfo edgeScene{};
        edgeScene.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        edgeScene.imageView   = m_ldrView[i];
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
        neighborhoodImages[0].imageView   = m_ldrView[i];
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
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd),
        "Failed to allocate dev font upload command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &beginInfo),
        "Failed to begin dev font upload command buffer");
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end dev font upload command buffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &fence),
        "Failed to create dev font upload fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence),
        "Failed to submit dev font upload command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for dev font upload fence");
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
        vkCheck(vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailable[i]),
            "Failed to create image-available semaphore");
        vkCheck(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlight[i]),
            "Failed to create in-flight fence");
    }
    for (auto& sem : m_renderFinished)
        vkCheck(vkCreateSemaphore(m_device, &semInfo, nullptr, &sem),
            "Failed to create render-finished semaphore");
}

// ============================================================
//  Depth resources
// ============================================================
void VulkanContext::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    // SAMPLED: the TAA resolve reads the final (post-water) depth for reprojection.
    createImage(m_swapchainExtent.width, m_swapchainExtent.height, depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
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

    m_sceneDepthCopyImage.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneDepthCopyMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneDepthCopyView.resize(MAX_FRAMES_IN_FLIGHT);
    m_sceneDepthCopyReady.assign(MAX_FRAMES_IN_FLIGHT, false);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createImage(m_swapchainExtent.width, m_swapchainExtent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_sceneDepthCopyImage[i], m_sceneDepthCopyMemory[i]);

        VkImageViewCreateInfo copyViewInfo = viewInfo;
        copyViewInfo.image = m_sceneDepthCopyImage[i];
        if (vkCreateImageView(m_device, &copyViewInfo, nullptr, &m_sceneDepthCopyView[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene depth copy image view");
    }
}

// ============================================================
//  Shadow map resources (image, render pass, framebuffer)
// ============================================================
void VulkanContext::createShadowResources() {
    VkFormat depthFormat = findDepthFormat();

    // Depth array: one layer per cascade. Sampled through a 2D_ARRAY view; each layer is
    // rendered into through its own single-layer 2D view.
    createImage(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_shadowImage, m_shadowImageMemory, 1, CSM_CASCADES);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_shadowImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = CSM_CASCADES;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_shadowImageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow array image view");

    for (uint32_t c = 0; c < CSM_CASCADES; c++) {
        VkImageViewCreateInfo lv = viewInfo;
        lv.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        lv.subresourceRange.baseArrayLayer = c;
        lv.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(m_device, &lv, nullptr, &m_shadowLayerView[c]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shadow cascade layer view");
    }

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
    fbInfo.width           = SHADOW_MAP_SIZE;
    fbInfo.height          = SHADOW_MAP_SIZE;
    fbInfo.layers          = 1;
    for (uint32_t c = 0; c < CSM_CASCADES; c++) {
        fbInfo.pAttachments = &m_shadowLayerView[c];
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_shadowFramebuffers[c]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shadow framebuffer");
    }
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

    // ShipVertex binding — shadow shader only reads pos (location 0)
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(ShipVertex);
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
    // Bindings 2-4 (grass/terrain) were removed with the farm world; ship textures keep
    // their original binding numbers 5-7 so the live ship shaders need no changes. Vulkan
    // permits the resulting non-contiguous binding set {0,1,5,6,7}.
    VkDescriptorSetLayoutBinding bindings[5]{};

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding         = 5; // imported ship albedo
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding         = 6; // imported ship normal
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding         = 7; // imported ship specular
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
        vkCheck(vkMapMemory(m_device, m_uniformBuffers[i].memory, 0, size, 0, &m_uniformBuffers[i].mapped),
            "Failed to map uniform buffer");
    }
}

void VulkanContext::createReflectionUniformBuffers() {
    VkDeviceSize size = sizeof(UniformBufferObject);
    m_reflectionUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_reflectionUniformBuffers[i] = createBuffer(size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkCheck(vkMapMemory(m_device, m_reflectionUniformBuffers[i].memory, 0, size, 0, &m_reflectionUniformBuffers[i].mapped),
            "Failed to map reflection uniform buffer");
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
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 8; // main + reflection scene descriptors (4 samplers each)

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

        VkDescriptorImageInfo shipAlbedoInfo{};
        shipAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shipAlbedoInfo.imageView   = m_shipAlbedoTex.view;
        shipAlbedoInfo.sampler     = m_shipAlbedoTex.sampler;

        VkDescriptorImageInfo shipNormalInfo{};
        shipNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shipNormalInfo.imageView   = m_shipNormalTex.view;
        shipNormalInfo.sampler     = m_shipNormalTex.sampler;

        VkDescriptorImageInfo shipSpecularInfo{};
        shipSpecularInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shipSpecularInfo.imageView   = m_shipSpecularTex.view;
        shipSpecularInfo.sampler     = m_shipSpecularTex.sampler;

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
        writes[2].dstBinding      = 5;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &shipAlbedoInfo;

        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_descriptorSets[i];
        writes[3].dstBinding      = 6;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &shipNormalInfo;

        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_descriptorSets[i];
        writes[4].dstBinding      = 7;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo      = &shipSpecularInfo;

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

        VkDescriptorImageInfo shipAlbedoInfo{};
        shipAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shipAlbedoInfo.imageView   = m_shipAlbedoTex.view;
        shipAlbedoInfo.sampler     = m_shipAlbedoTex.sampler;

        VkDescriptorImageInfo shipNormalInfo{};
        shipNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shipNormalInfo.imageView   = m_shipNormalTex.view;
        shipNormalInfo.sampler     = m_shipNormalTex.sampler;

        VkDescriptorImageInfo shipSpecularInfo{};
        shipSpecularInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shipSpecularInfo.imageView   = m_shipSpecularTex.view;
        shipSpecularInfo.sampler     = m_shipSpecularTex.sampler;

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
        writes[2].dstBinding      = 5;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &shipAlbedoInfo;

        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_reflectionDescriptorSets[i];
        writes[3].dstBinding      = 6;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &shipNormalInfo;

        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_reflectionDescriptorSets[i];
        writes[4].dstBinding      = 7;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo      = &shipSpecularInfo;

        vkUpdateDescriptorSets(m_device, 5, writes, 0, nullptr);
    }
}

void VulkanContext::createOceanDescriptors() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 10; // reflection + normals + FFT/wake maps + scene depth/color + history color/depth

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
        displacementInfo.imageView   = m_oceanDisplacementView[i]; // double-buffered per frame
        displacementInfo.sampler     = m_oceanDisplacementSampler;

        VkDescriptorImageInfo wakeInfo{};
        wakeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // storage image, kept in GENERAL
        wakeInfo.imageView   = m_oceanWakeView[i]; // current frame's wake simulation target
        wakeInfo.sampler     = m_oceanDisplacementSampler; // linear, repeat — shared ocean sampler

        VkDescriptorImageInfo slopeInfo{};
        slopeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // storage image, kept in GENERAL
        slopeInfo.imageView   = m_oceanSlopeView[i]; // double-buffered per frame
        slopeInfo.sampler     = m_oceanDisplacementSampler; // linear, repeat — shared with displacement

        VkDescriptorImageInfo sceneDepthInfo{};
        sceneDepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneDepthInfo.imageView   = m_sceneDepthCopyView[i];
        sceneDepthInfo.sampler     = m_sceneDepthSampler;

        VkDescriptorImageInfo sceneColorInfo{};
        sceneColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneColorInfo.imageView   = m_sceneColorCopyView[i];
        sceneColorInfo.sampler     = m_postSampler;

        VkDescriptorImageInfo sceneHistoryInfo{};
        sceneHistoryInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneHistoryInfo.imageView   = m_sceneColorCopyView[i];
        sceneHistoryInfo.sampler     = m_postSampler;

        VkDescriptorImageInfo sceneHistoryDepthInfo{};
        sceneHistoryDepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneHistoryDepthInfo.imageView   = m_sceneDepthCopyView[i];
        sceneHistoryDepthInfo.sampler     = m_sceneDepthSampler;

        VkWriteDescriptorSet writes[11]{};
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

        writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = m_oceanDescriptorSets[i];
        writes[5].dstBinding      = 5;
        writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].descriptorCount = 1;
        writes[5].pImageInfo      = &wakeInfo;

        writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet          = m_oceanDescriptorSets[i];
        writes[6].dstBinding      = 6;
        writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].descriptorCount = 1;
        writes[6].pImageInfo      = &slopeInfo;

        writes[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet          = m_oceanDescriptorSets[i];
        writes[7].dstBinding      = 7;
        writes[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].descriptorCount = 1;
        writes[7].pImageInfo      = &sceneDepthInfo;

        writes[8].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet          = m_oceanDescriptorSets[i];
        writes[8].dstBinding      = 8;
        writes[8].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[8].descriptorCount = 1;
        writes[8].pImageInfo      = &sceneColorInfo;

        writes[9].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet          = m_oceanDescriptorSets[i];
        writes[9].dstBinding      = 9;
        writes[9].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].descriptorCount = 1;
        writes[9].pImageInfo      = &sceneHistoryInfo;

        writes[10].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet          = m_oceanDescriptorSets[i];
        writes[10].dstBinding      = 10;
        writes[10].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].descriptorCount = 1;
        writes[10].pImageInfo      = &sceneHistoryDepthInfo;

        vkUpdateDescriptorSets(m_device, 11, writes, 0, nullptr);
    }
}

void VulkanContext::updateOceanHistoryDescriptor(uint32_t currentFrame) {
    const uint32_t historyIndex =
        (currentFrame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;

    VkDescriptorImageInfo historyInfo{};
    historyInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    historyInfo.imageView = (m_temporalHistoryFrames > 0)
        ? m_offscreenView[historyIndex]
        : m_sceneColorCopyView[currentFrame];
    historyInfo.sampler = m_postSampler;

    VkDescriptorImageInfo historyDepthInfo{};
    historyDepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    historyDepthInfo.imageView = (m_temporalHistoryFrames > 0)
        ? m_sceneDepthCopyView[historyIndex]
        : m_sceneDepthCopyView[currentFrame];
    historyDepthInfo.sampler = m_sceneDepthSampler;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_oceanDescriptorSets[currentFrame];
    writes[0].dstBinding      = 9;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo      = &historyInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_oceanDescriptorSets[currentFrame];
    writes[1].dstBinding      = 10;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo      = &historyDepthInfo;

    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
}

