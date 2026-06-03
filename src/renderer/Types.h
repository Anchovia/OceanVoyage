#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

enum class TileType : uint8_t {
    AIR = 0,
    GRASS,
    DIRT,
    WATER,
    STONE,
    WOOD,
    LEAVES,
    FARMLAND,
    WHEAT,
};

enum class ItemType : uint8_t {
    NONE = 0,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_WOOD,
    BLOCK_LEAVES,
    BLOCK_WATER,
    TOOL_HOE,
    TOOL_AXE,
    SEED_WHEAT,
    TOOL_WATERINGCAN,
    TOOL_SICKLE,
    TOOL_PICKAXE,
    ITEM_WHEAT,
    ITEM_WORKBENCH,
    ITEM_FENCE,
    ITEM_STONE_FENCE,
    COUNT,
};

inline bool isBlock(ItemType t) {
    return t >= ItemType::BLOCK_GRASS && t <= ItemType::BLOCK_WATER;
}
inline bool isTool(ItemType t) {
    return t == ItemType::TOOL_HOE || t == ItemType::TOOL_AXE || t == ItemType::TOOL_WATERINGCAN
        || t == ItemType::TOOL_SICKLE || t == ItemType::TOOL_PICKAXE;
}
inline TileType itemToTile(ItemType t) {
    switch (t) {
        case ItemType::BLOCK_GRASS:  return TileType::GRASS;
        case ItemType::BLOCK_DIRT:   return TileType::DIRT;
        case ItemType::BLOCK_STONE:  return TileType::STONE;
        case ItemType::BLOCK_WOOD:   return TileType::WOOD;
        case ItemType::BLOCK_LEAVES: return TileType::LEAVES;
        case ItemType::BLOCK_WATER:  return TileType::WATER;
        default:                     return TileType::AIR;
    }
}
inline glm::vec3 itemColor(ItemType t) {
    switch (t) {
        case ItemType::BLOCK_GRASS:  return {0.45f, 0.75f, 0.30f};
        case ItemType::BLOCK_DIRT:   return {0.55f, 0.35f, 0.15f};
        case ItemType::BLOCK_STONE:  return {0.55f, 0.55f, 0.55f};
        case ItemType::BLOCK_WOOD:   return {0.42f, 0.28f, 0.15f};
        case ItemType::BLOCK_LEAVES: return {0.30f, 0.55f, 0.25f};
        case ItemType::BLOCK_WATER:  return {0.20f, 0.45f, 0.70f};
        case ItemType::TOOL_HOE:     return {0.80f, 0.70f, 0.50f};
        case ItemType::TOOL_AXE:     return {0.50f, 0.50f, 0.55f};
        case ItemType::SEED_WHEAT:   return {0.80f, 0.75f, 0.20f};
        case ItemType::TOOL_WATERINGCAN: return {0.30f, 0.55f, 0.80f};
        case ItemType::TOOL_SICKLE:  return {0.70f, 0.72f, 0.45f};
        case ItemType::TOOL_PICKAXE: return {0.45f, 0.48f, 0.55f};
        case ItemType::ITEM_WHEAT:   return {0.90f, 0.75f, 0.15f};
        case ItemType::ITEM_WORKBENCH: return {0.55f, 0.38f, 0.18f};
        case ItemType::ITEM_FENCE:   return {0.62f, 0.45f, 0.25f};
        case ItemType::ITEM_STONE_FENCE: return {0.58f, 0.58f, 0.60f};
        default:                     return {0.0f,  0.0f,  0.0f};
    }
}

// Inventory item stack (a slot holds one item type + a count)
struct ItemStack {
    ItemType type  = ItemType::NONE;
    int      count = 0;
};

// An item lying in the world: spawned on harvest, picked up on player proximity
struct DroppedItem {
    glm::vec3 pos   = {0.0f, 0.0f, 0.0f};
    ItemType  type  = ItemType::NONE;
    int       count = 0;
};

// Inventory grid layout (shared between GameState and VulkanContext)
static constexpr int   INV_COLS      = 9;   // first row == hotbar
static constexpr int   INV_ROWS      = 3;
static constexpr float INV_SLOT_SIZE = 52.0f;
static constexpr float INV_GAP       = 8.0f;
static constexpr float INV_PAD       = 16.0f;

static constexpr int HOTBAR_SLOTS = 9;
static constexpr int INV_SLOTS    = INV_COLS * INV_ROWS; // 27 (hotbar 0..8 + backpack)

// ---- Crafting ----
struct RecipeInput { ItemType type = ItemType::NONE; int count = 0; };
struct Recipe {
    ItemType    result;
    int         resultCount;
    RecipeInput inputs[3];        // type == NONE terminates the list
    bool        requiresWorkbench; // false = craftable from inventory anywhere
};

// Stardew-style recipe list: click a recipe to consume materials and produce the result.
inline const Recipe* craftingRecipes(int& outCount) {
    static const Recipe table[] = {
        { ItemType::ITEM_WORKBENCH,   1, {{ItemType::BLOCK_WOOD, 4},  {}, {}}, false },
        { ItemType::ITEM_FENCE,       1, {{ItemType::BLOCK_WOOD, 2},  {}, {}}, false },
        { ItemType::ITEM_STONE_FENCE, 1, {{ItemType::BLOCK_STONE, 2}, {}, {}}, true  }, // needs workbench
    };
    outCount = (int)(sizeof(table) / sizeof(table[0]));
    return table;
}

// Crafting panel layout — shared so click detection (GameState) matches rendering (VulkanContext).
static constexpr float CRAFT_ROW_W   = 260.0f;
static constexpr float CRAFT_ROW_H   = 44.0f;
static constexpr float CRAFT_ROW_GAP = 6.0f;

// Screen-space rect of crafting row `i`; panel sits centered below the inventory grid.
inline void craftRowRect(int i, float screenW, float screenH, float& x, float& y, float& w, float& h) {
    const float gridH  = INV_ROWS * INV_SLOT_SIZE + (INV_ROWS - 1) * INV_GAP;
    const float panelH = gridH + 2 * INV_PAD;
    const float invTop = (screenH - gridH) * 0.5f - INV_PAD;
    w = CRAFT_ROW_W;
    h = CRAFT_ROW_H;
    x = (screenW - w) * 0.5f;
    y = invTop + panelH + 12.0f + i * (h + CRAFT_ROW_GAP);
}

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

// Screen-space rect of settings row `i`: 0=VSync, 1=AA, 2=Back.
inline void settingsRowRect(int i, float screenW, float screenH, float& x, float& y, float& w, float& h) {
    w = SETTINGS_ROW_W;
    h = SETTINGS_ROW_H;
    x = (screenW - w) * 0.5f;
    y = screenH * 0.5f - 12.0f + i * (h + SETTINGS_ROW_GAP);
}

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
};

struct InstanceData {
    glm::vec3 pos;
    glm::vec3 topColor;
    glm::vec3 sideColor;
};

// Chunk mesh vertex — color baked per-vertex (no instancing)
struct ChunkVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;  // top face uses topColor, side/bottom use sideColor (kept as tint)
    glm::vec2 uv;     // per-face 0..1
    float     layer;  // material texture-array layer; < 0 = untextured
};

// Terrain texture-array layers — one material per layer. tileFaceLayer maps a tile
// type + face to its layer index. Objects may reuse these material layers.
static constexpr uint32_t TERRAIN_TEX_LAYERS = 9;
inline int tileFaceLayer(TileType t, bool isTop) {
    switch (t) {
        case TileType::GRASS:    return isTop ? 0 : 1; // grassy top / dirt-ish side
        case TileType::DIRT:     return 2;
        case TileType::STONE:    return 3;
        case TileType::WOOD:     return 4;
        case TileType::LEAVES:   return 5;
        case TileType::FARMLAND: return 6;
        case TileType::WHEAT:    return 7;
        case TileType::WATER:    return 8;
        default:                 return -1;
    }
}

// Grass alpha-card vertex — local card position + lighting normal + texture UV
struct GrassCardVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// UI vertex — screen-space NDC position + RGBA color
struct UIVertex {
    glm::vec2 pos;
    glm::vec4 color;
};

// Object instance transform for low-poly props and visual dressing.
struct ObjectInstance {
    glm::vec3 pos;
    float     scale;
    float     rot;   // radians around Z
};
