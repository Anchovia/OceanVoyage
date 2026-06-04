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
    glm::vec4 animationParams; // x = game time seconds, yzw reserved
    glm::vec4 cameraPos; // xyz = camera world position (ocean Fresnel / specular)
    glm::mat4 reflectionViewProj; // planar water reflection projection for ocean sampling
};

struct PostPushConstants {
    glm::vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
};

// ============================================================
//  Cube vertex data (24 verts, 4 per face — flat-shaded)
// ============================================================
#define FACE(ax,ay,az, bx,by,bz, cx,cy,cz, dx,dy,dz, nx,ny,nz) \
    {{ax,ay,az},{nx,ny,nz}}, \
    {{bx,by,bz},{nx,ny,nz}}, \
    {{cx,cy,cz},{nx,ny,nz}}, \
    {{dx,dy,dz},{nx,ny,nz}}

static const std::vector<Vertex> kVertices = {
    FACE(-0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,  0, 0, 1),  // Top
    FACE(-0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f,  0, 0,-1),  // Bottom
    FACE(-0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f,  0,-1, 0),  // Front
    FACE( 0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0, 1, 0),  // Back
    FACE( 0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  1, 0, 0),  // Right
    FACE(-0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -1, 0, 0),  // Left
};
#undef FACE

static const std::vector<uint16_t> kIndices = {
     0, 1, 2,   0, 2, 3,   // Top
     4, 5, 6,   4, 6, 7,   // Bottom
     8, 9,10,   8,10,11,   // Front
    12,13,14,  12,14,15,   // Back
    16,17,18,  16,18,19,   // Right
    20,21,22,  20,22,23,   // Left
};

// ============================================================
//  Selector outline vertex data
// ============================================================
static const std::vector<Vertex> kSelectorVertices = {
    {{-0.48f,  0.40f, 0.54f}, {0, 0, 1}},
    {{ 0.48f,  0.40f, 0.54f}, {0, 0, 1}},
    {{ 0.48f,  0.48f, 0.54f}, {0, 0, 1}},
    {{-0.48f,  0.48f, 0.54f}, {0, 0, 1}},

    {{ 0.40f, -0.48f, 0.54f}, {0, 0, 1}},
    {{ 0.48f, -0.48f, 0.54f}, {0, 0, 1}},
    {{ 0.48f,  0.48f, 0.54f}, {0, 0, 1}},
    {{ 0.40f,  0.48f, 0.54f}, {0, 0, 1}},

    {{-0.48f, -0.48f, 0.54f}, {0, 0, 1}},
    {{ 0.48f, -0.48f, 0.54f}, {0, 0, 1}},
    {{ 0.48f, -0.40f, 0.54f}, {0, 0, 1}},
    {{-0.48f, -0.40f, 0.54f}, {0, 0, 1}},

    {{-0.48f, -0.48f, 0.54f}, {0, 0, 1}},
    {{-0.40f, -0.48f, 0.54f}, {0, 0, 1}},
    {{-0.40f,  0.48f, 0.54f}, {0, 0, 1}},
    {{-0.48f,  0.48f, 0.54f}, {0, 0, 1}},
};

static const std::vector<uint16_t> kSelectorIndices = {
     0,  1,  2,   0,  2,  3,
     4,  5,  6,   4,  6,  7,
     8,  9, 10,   8, 10, 11,
    12, 13, 14,  12, 14, 15,
};

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
