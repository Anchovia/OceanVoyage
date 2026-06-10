#pragma once
#include "world/Chunk.h"
#include <glm/glm.hpp>
#include <unordered_map>

class World {
public:
    World();

    TileType  getTile(int x, int y, int z) const;
    void      setTile(int x, int y, int z, TileType t);

    TileState getTileState(int x, int y, int z) const;
    void      setTileState(int x, int y, int z, const TileState& s);

    void growthTick(int currentDay);

    enum class HarvestResult { NoObject, WrongTool, Harvested };
    // Tries to harvest a world object (tree/rock) standing on tile (x,y).
    // On Harvested, fills the drop info and removes the object.
    HarvestResult tryHarvestObject(int x, int y, ItemType tool,
                                   glm::vec3& outPos, ItemType& outDrop, int& outCount);

    bool hasObjectAt(int x, int y) const;
    // True if a collidable object sits on tile (x,y) (blocks player movement).
    bool isCollidableAt(int x, int y) const;
    // True if an object of the given type sits within `radius` tiles of (x,y).
    bool isObjectTypeNear(int x, int y, ObjectType type, int radius) const;
    // Places an object on top of the ground at (x,y). Fails if the tile is not
    // solid ground, is water, or already holds an object.
    bool placeObject(int x, int y, ObjectType type);

    bool       inBounds(int x, int y, int z) const;
    bool       isWalkable(int x, int y, int z) const;
    glm::ivec3 worldToTile(const glm::vec3& position) const;
    glm::vec3  tileCenter(int x, int y, int z) const;

    static glm::vec3 tileColor(TileType type, uint8_t growthStage = 0);
    static glm::vec3 tileSideColor(TileType type);

    void loadChunksAround(int cx, int cy, int radius);
    int  loadChunksAroundBudgeted(int cx, int cy, int radius, int maxChunks);
    void unloadChunksOutside(int cx, int cy, int radius);
    // Drops all in-memory chunks (loaded + modified-unsaved). Used when ending a
    // world session (e.g. quitting to title) so the next session starts from disk.
    void reset();

    static glm::ivec2 chunkCoord(int x, int y);

    const std::unordered_map<glm::ivec2, Chunk, IVec2Hash>& chunks() const { return m_chunks; }
          std::unordered_map<glm::ivec2, Chunk, IVec2Hash>& chunks()       { return m_chunks; }

private:
    std::unordered_map<glm::ivec2, Chunk, IVec2Hash> m_modifiedUnloaded;
    void         generateChunk(int cx, int cy);
    Chunk&       getOrCreateChunk(int cx, int cy);
    const Chunk* getChunk(int cx, int cy) const;

    static glm::ivec2 localCoord(int x, int y);

    std::unordered_map<glm::ivec2, Chunk, IVec2Hash> m_chunks;
};
