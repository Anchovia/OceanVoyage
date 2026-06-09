#pragma once
// Shared private constants, types, and helpers for VulkanContext_*.cpp files.
// Always include VulkanContext.h before this header (it brings in Vulkan + GLM).

#include "renderer/Types.h"
#include <iostream>
#include <vector>

// ============================================================
//  Shared UBO type
// ============================================================
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDir; // xyz = toward sun, w = dayFactor (0=night, 1=noon)
    glm::mat4 lightMVP; // light-space transform for shadow map lookup
    glm::vec4 fogColor; // rgb = sky color at current time of day
    glm::vec4 clipPlane; // xyz = world-space plane normal, w = plane distance
    glm::vec4 animationParams; // x = game time seconds, yzw reserved
    glm::vec4 cameraPos; // xyz = camera world position (ocean Fresnel / specular)
    glm::mat4 reflectionViewProj; // planar water reflection projection for ocean sampling
    glm::mat4 invViewProj; // inverse camera projection-view for full-screen sky reconstruction
    glm::mat4 prevViewProj; // previous main-camera projection-view for temporal reprojection
    glm::vec4 temporalParams; // x = history valid, yzw reserved
    // CSM (appended at the end so shaders that don't sample shadows keep their layout). The
    // legacy single `lightMVP` above is left in place for layout stability and holds cascade 0.
    glm::mat4 lightMVPCascade[3]; // per-cascade light-space transforms
    glm::vec4 cascadeSplits;      // xyz = cascade far view-depths (view space); w unused
};

struct PostPushConstants {
    glm::vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
};

// (The flat-shaded unit-cube mesh that lived here — kVertices/kIndices — was removed
//  along with the legacy player cube / selector / drops that were its only users.)

// ============================================================
//  Validation / extension lists
// ============================================================
static const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
static const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
#ifdef NDEBUG
static constexpr bool kEnableValidation = false;
#else
static constexpr bool kEnableValidation = true;
#endif

// ============================================================
//  Debug messenger free-function helpers
// ============================================================
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << data->pMessage << "\n";
    return VK_FALSE;
}

static VkResult CreateDebugMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* info,
    VkDebugUtilsMessengerEXT* out)
{
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    return fn ? fn(instance, info, nullptr, out) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(instance, messenger, nullptr);
}
