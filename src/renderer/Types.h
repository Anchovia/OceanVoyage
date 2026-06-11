#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

static constexpr float SETTINGS_ROW_W   = 300.0f;
static constexpr float SETTINGS_ROW_H   = 38.0f;
static constexpr float SETTINGS_ROW_GAP = 14.0f;

// Screen-space rect of main-menu row `i`: 0=Start, 1=Settings.
inline void mainMenuRowRect(int i, float screenW, float screenH, float& x, float& y, float& w, float& h) {
    w = SETTINGS_ROW_W;
    h = SETTINGS_ROW_H;
    x = (screenW - w) * 0.5f;
    y = screenH * 0.5f + 8.0f + i * (h + SETTINGS_ROW_GAP);
}

// Screen-space rect of pause-menu row `i`: 0=Resume, 1=Settings, 2=Quit.
inline void pauseMenuRowRect(int i, float screenW, float screenH, float& x, float& y, float& w, float& h) {
    w = SETTINGS_ROW_W;
    h = SETTINGS_ROW_H;
    x = (screenW - w) * 0.5f;
    y = screenH * 0.5f - 12.0f + i * (h + SETTINGS_ROW_GAP);
}

// Screen-space rect of settings row `i`: 0=VSync, 1=AA, 2=Reflection, 3=Back.
inline void settingsRowRect(int i, float screenW, float screenH, float& x, float& y, float& w, float& h) {
    w = SETTINGS_ROW_W;
    h = SETTINGS_ROW_H;
    x = (screenW - w) * 0.5f;
    y = screenH * 0.5f - 12.0f + i * (h + SETTINGS_ROW_GAP);
}

// Screen-space rect of port-menu row `i`: 0=Set Sail, 1=Trade.
inline void portMenuRowRect(int i, float screenW, float screenH, float& x, float& y, float& w, float& h) {
    w = SETTINGS_ROW_W;
    h = SETTINGS_ROW_H;
    x = (screenW - w) * 0.5f;
    y = screenH * 0.5f - 12.0f + i * (h + SETTINGS_ROW_GAP);
}

// Imported hero ship vertex: textured material path with tangent-space normal mapping.
struct ShipVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec2 uv;
};

// Procedural material-lite vertex for static port props.
struct PortVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 color; // rgb = albedo, a = emissive strength
};

// UI vertex — screen-space NDC position + RGBA color
struct UIVertex {
    glm::vec2 pos;
    glm::vec4 color;
};
