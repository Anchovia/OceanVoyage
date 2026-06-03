#pragma once
#include "renderer/Types.h"
#include <glm/glm.hpp>
#include <functional>
#include <vector>

static constexpr int CHUNK_SIZE  = 32;
static constexpr int CHUNK_DEPTH = 8;

struct TileState {
    uint8_t  growthStage    = 0;
    uint32_t lastUpdatedDay = 0;
    bool     watered        = false; // farmland; resets daily, persisted in save (v3)
};

enum class ObjectType : uint8_t {
    TREE = 0,
    ROCK,
    WORKBENCH,
    FENCE,
    STONE_FENCE,
    COUNT,
};

// Data-driven properties per object type (mesh is built separately in the renderer).
// harvestTool / dropItem / placeable are consumed by the gathering & building systems.
struct ObjectDef {
    bool     castShadow  = true;
    bool     collidable  = false;          // stored; movement enforcement is a later pass
    bool     placeable   = false;          // player-buildable (used by the building system)
    ItemType harvestTool = ItemType::NONE; // tool required to harvest (NONE = not harvestable)
    ItemType dropItem    = ItemType::NONE; // item produced on harvest
    int      dropCount   = 0;
};

inline const ObjectDef& objectDef(ObjectType type) {
    static const ObjectDef defs[(size_t)ObjectType::COUNT] = {
        /* TREE      */ { true, true, false, ItemType::TOOL_AXE,     ItemType::BLOCK_WOOD,    1 },
        /* ROCK      */ { true, true, false, ItemType::TOOL_PICKAXE, ItemType::BLOCK_STONE,   1 },
        /* WORKBENCH */ { true, true, true,  ItemType::NONE,         ItemType::ITEM_WORKBENCH,   1 },
        /* FENCE     */ { true, true, true,  ItemType::NONE,         ItemType::ITEM_FENCE,       1 },
        /* STONE_FEN */ { true, true, true,  ItemType::NONE,         ItemType::ITEM_STONE_FENCE, 1 },
    };
    return defs[(size_t)type];
}

// Maps a placeable inventory item to the object type it places. Returns false if not placeable.
inline bool itemToObjectType(ItemType item, ObjectType& out) {
    switch (item) {
        case ItemType::ITEM_WORKBENCH:   out = ObjectType::WORKBENCH;   return true;
        case ItemType::ITEM_FENCE:       out = ObjectType::FENCE;       return true;
        case ItemType::ITEM_STONE_FENCE: out = ObjectType::STONE_FENCE; return true;
        default:                         return false;
    }
}

// World prop placed on top of the tile grid (rendered as a low-poly model, not a voxel)
struct Object {
    glm::vec3  pos;    // world position of the base
    float      scale;
    float      rot;    // radians around Z
    ObjectType type;
};

struct Chunk {
    TileType  tiles [CHUNK_DEPTH][CHUNK_SIZE][CHUNK_SIZE] = {};
    TileState states[CHUNK_DEPTH][CHUNK_SIZE][CHUNK_SIZE] = {};
    std::vector<Object> objects;
    bool dirty        = true;
    bool objectsDirty = true;  // objects changed → renderer rebuilds the instance buffers
    bool grassDirty   = true;  // grass dressing changed → renderer rebuilds the grass buffer
    bool modified     = false; // true if player has changed any tile (used for save/load)
};

struct IVec2Hash {
    size_t operator()(const glm::ivec2& v) const {
        size_t h1 = std::hash<int>()(v.x);
        size_t h2 = std::hash<int>()(v.y);
        return h1 ^ (h2 * 2654435761u);
    }
};
