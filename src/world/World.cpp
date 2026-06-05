#include "World.h"
#include "TerrainGen.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>

static const glm::vec3 kTileColors[] = {
    {0.0f,  0.0f,  0.0f },  // AIR
    {0.45f, 0.75f, 0.30f},  // GRASS
    {0.55f, 0.35f, 0.15f},  // DIRT
    {0.20f, 0.45f, 0.70f},  // WATER
    {0.55f, 0.55f, 0.55f},  // STONE
    {0.42f, 0.28f, 0.15f},  // WOOD
    {0.30f, 0.55f, 0.25f},  // LEAVES
    {0.30f, 0.18f, 0.08f},  // FARMLAND
    {0.58f, 0.75f, 0.32f},  // WHEAT (stage 0 fallback; overridden by growthStage)
};

World::World() {
    // Chunks are generated on demand via loadChunksAround()
}

glm::ivec2 World::chunkCoord(int x, int y) {
    return {
        (int)std::floor((float)x / CHUNK_SIZE),
        (int)std::floor((float)y / CHUNK_SIZE)
    };
}

glm::ivec2 World::localCoord(int x, int y) {
    return {
        ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
        ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE
    };
}

void World::generateChunk(int cx, int cy) {
    Chunk& chunk = getOrCreateChunk(cx, cy);
    TerrainGen::generate(cx, cy, chunk);
}

void World::loadChunksAround(int cx, int cy, int radius) {
    const int diameter = radius * 2 + 1;
    loadChunksAroundBudgeted(cx, cy, radius, diameter * diameter);
}

int World::loadChunksAroundBudgeted(int cx, int cy, int radius, int maxChunks) {
    if (maxChunks <= 0) return 0;

    auto loadChunk = [&](const glm::ivec2& coord) -> bool {
        if (m_chunks.find(coord) != m_chunks.end()) return false;

        auto it = m_modifiedUnloaded.find(coord);
        if (it != m_modifiedUnloaded.end()) {
            m_chunks[coord] = std::move(it->second);
            m_modifiedUnloaded.erase(it);
        } else {
            generateChunk(coord.x, coord.y);
        }
        return true;
    };

    int loaded = 0;
    for (int ring = 0; ring <= radius; ring++) {
        for (int dy = -ring; dy <= ring; dy++)
        for (int dx = -ring; dx <= ring; dx++) {
            if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
            if (loadChunk({cx + dx, cy + dy}) && ++loaded >= maxChunks)
                return loaded;
        }
    }
    return loaded;
}

void World::unloadChunksOutside(int cx, int cy, int radius) {
    for (auto it = m_chunks.begin(); it != m_chunks.end(); ) {
        if (std::abs(it->first.x - cx) > radius ||
            std::abs(it->first.y - cy) > radius) {
            if (it->second.modified) {
                it->second.dirty = true;
                m_modifiedUnloaded[it->first] = std::move(it->second);
            }
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }
}

void World::reset() {
    m_chunks.clear();
    m_modifiedUnloaded.clear();
}

Chunk& World::getOrCreateChunk(int cx, int cy) {
    return m_chunks[{cx, cy}];
}

const Chunk* World::getChunk(int cx, int cy) const {
    auto it = m_chunks.find({cx, cy});
    return it != m_chunks.end() ? &it->second : nullptr;
}

TileType World::getTile(int x, int y, int z) const {
    if (z < 0 || z >= CHUNK_DEPTH) return TileType::AIR;
    auto cc = chunkCoord(x, y);
    const Chunk* chunk = getChunk(cc.x, cc.y);
    if (!chunk) return TileType::AIR;
    auto lc = localCoord(x, y);
    return chunk->tiles[z][lc.y][lc.x];
}

void World::setTile(int x, int y, int z, TileType t) {
    if (z < 0 || z >= CHUNK_DEPTH) return;
    auto cc = chunkCoord(x, y);
    Chunk& chunk = getOrCreateChunk(cc.x, cc.y);
    auto lc = localCoord(x, y);
    chunk.tiles[z][lc.y][lc.x] = t;
    chunk.dirty      = true;
    chunk.grassDirty = true;
    chunk.modified   = true;
}

TileState World::getTileState(int x, int y, int z) const {
    if (z < 0 || z >= CHUNK_DEPTH) return {};
    auto cc = chunkCoord(x, y);
    const Chunk* chunk = getChunk(cc.x, cc.y);
    if (!chunk) return {};
    auto lc = localCoord(x, y);
    return chunk->states[z][lc.y][lc.x];
}

void World::setTileState(int x, int y, int z, const TileState& s) {
    if (z < 0 || z >= CHUNK_DEPTH) return;
    auto cc = chunkCoord(x, y);
    Chunk& chunk = getOrCreateChunk(cc.x, cc.y);
    auto lc = localCoord(x, y);
    chunk.states[z][lc.y][lc.x] = s;
    chunk.dirty    = true;   // state can affect rendering (e.g. watered farmland tint)
    chunk.modified = true;
}

// ---- Save / Load ----

static constexpr char    kMagic[5]    = "PFRM";
static constexpr uint8_t kSaveVer     = 3; // v3: inventory + drops + watered serialized
static constexpr int32_t kMaxEntries  = 1 << 24; // sanity cap to reject corrupt counts

void World::save(const std::string& path, const glm::vec3& playerPos, float gameTime,
                 const std::array<ItemStack, INV_SLOTS>& inventory,
                 const std::vector<DroppedItem>& drops) const {
    // Atomic write: serialize to a temp file, flush/close, then rename over the
    // target. A crash mid-write can only damage the temp, never the live save.
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary);
        if (!f) return;

        f.write(kMagic, 5);
        f.write(reinterpret_cast<const char*>(&kSaveVer), 1);
        f.write(reinterpret_cast<const char*>(&playerPos.x), 4);
        f.write(reinterpret_cast<const char*>(&playerPos.y), 4);
        f.write(reinterpret_cast<const char*>(&playerPos.z), 4);
        f.write(reinterpret_cast<const char*>(&gameTime), 4);

        // Inventory (fixed INV_SLOTS slots)
        int32_t invCount = (int32_t)INV_SLOTS;
        f.write(reinterpret_cast<const char*>(&invCount), 4);
        for (const ItemStack& s : inventory) {
            uint8_t t = (uint8_t)s.type;
            int32_t c = (int32_t)s.count;
            f.write(reinterpret_cast<const char*>(&t), 1);
            f.write(reinterpret_cast<const char*>(&c), 4);
        }

        // Dropped items lying in the world
        int32_t dropCount = (int32_t)drops.size();
        f.write(reinterpret_cast<const char*>(&dropCount), 4);
        for (const DroppedItem& d : drops) {
            uint8_t t = (uint8_t)d.type;
            int32_t c = (int32_t)d.count;
            f.write(reinterpret_cast<const char*>(&t), 1);
            f.write(reinterpret_cast<const char*>(&c), 4);
            f.write(reinterpret_cast<const char*>(&d.pos.x), 4);
            f.write(reinterpret_cast<const char*>(&d.pos.y), 4);
            f.write(reinterpret_cast<const char*>(&d.pos.z), 4);
        }

        int32_t count = 0;
        for (const auto& [coord, chunk] : m_chunks)
            if (chunk.modified) count++;
        count += (int32_t)m_modifiedUnloaded.size();
        f.write(reinterpret_cast<const char*>(&count), 4);

        auto writeChunk = [&](const glm::ivec2& coord, const Chunk& chunk) {
            f.write(reinterpret_cast<const char*>(&coord.x), 4);
            f.write(reinterpret_cast<const char*>(&coord.y), 4);
            f.write(reinterpret_cast<const char*>(chunk.tiles), sizeof(chunk.tiles));
            for (int z = 0; z < CHUNK_DEPTH; z++)
            for (int y = 0; y < CHUNK_SIZE;  y++)
            for (int x = 0; x < CHUNK_SIZE;  x++)
                f.write(reinterpret_cast<const char*>(&chunk.states[z][y][x].growthStage), 1);
            for (int z = 0; z < CHUNK_DEPTH; z++)
            for (int y = 0; y < CHUNK_SIZE;  y++)
            for (int x = 0; x < CHUNK_SIZE;  x++)
                f.write(reinterpret_cast<const char*>(&chunk.states[z][y][x].lastUpdatedDay), 4);
            for (int z = 0; z < CHUNK_DEPTH; z++)
            for (int y = 0; y < CHUNK_SIZE;  y++)
            for (int x = 0; x < CHUNK_SIZE;  x++) {
                uint8_t w = chunk.states[z][y][x].watered ? 1 : 0;
                f.write(reinterpret_cast<const char*>(&w), 1);
            }

            // Objects — captures placed structures + remaining natural props (post-harvest)
            int32_t objCount = (int32_t)chunk.objects.size();
            f.write(reinterpret_cast<const char*>(&objCount), 4);
            for (const Object& o : chunk.objects) {
                uint8_t t = (uint8_t)o.type;
                f.write(reinterpret_cast<const char*>(&t), 1);
                f.write(reinterpret_cast<const char*>(&o.pos.x), 4);
                f.write(reinterpret_cast<const char*>(&o.pos.y), 4);
                f.write(reinterpret_cast<const char*>(&o.pos.z), 4);
                f.write(reinterpret_cast<const char*>(&o.scale), 4);
                f.write(reinterpret_cast<const char*>(&o.rot), 4);
            }
        };

        for (const auto& [coord, chunk] : m_chunks)
            if (chunk.modified) writeChunk(coord, chunk);
        for (const auto& [coord, chunk] : m_modifiedUnloaded)
            writeChunk(coord, chunk);

        if (!f) { // a write failed — leave the live save untouched
            f.close();
            std::error_code rmEc;
            std::filesystem::remove(tmpPath, rmEc);
            return;
        }
    } // ofstream flushed & closed here

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
    }
}

bool World::load(const std::string& path, glm::vec3& outPlayerPos, float& outGameTime,
                 std::array<ItemStack, INV_SLOTS>& outInventory,
                 std::vector<DroppedItem>& outDrops) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[5];
    f.read(magic, 5);
    if (!f || std::memcmp(magic, kMagic, 5) != 0) return false;

    uint8_t ver;
    f.read(reinterpret_cast<char*>(&ver), 1);
    if (!f || ver != kSaveVer) return false; // version mismatch → treated as new world (dev policy)

    // Read everything into locals first; commit only on full success so a corrupt
    // or truncated save never leaves the world/inventory partially mutated.
    glm::vec3 playerPos;
    float     gameTime;
    f.read(reinterpret_cast<char*>(&playerPos.x), 4);
    f.read(reinterpret_cast<char*>(&playerPos.y), 4);
    f.read(reinterpret_cast<char*>(&playerPos.z), 4);
    f.read(reinterpret_cast<char*>(&gameTime), 4);
    if (!f) return false;

    // Inventory
    std::array<ItemStack, INV_SLOTS> inventory{};
    int32_t invCount;
    f.read(reinterpret_cast<char*>(&invCount), 4);
    if (!f || invCount != (int32_t)INV_SLOTS) return false;
    for (int i = 0; i < INV_SLOTS; i++) {
        uint8_t t; int32_t c;
        f.read(reinterpret_cast<char*>(&t), 1);
        f.read(reinterpret_cast<char*>(&c), 4);
        if (!f || t >= (uint8_t)ItemType::COUNT || c < 0) return false;
        inventory[i].type  = (ItemType)t;
        inventory[i].count = (t == 0) ? 0 : c; // NONE slot is always empty
    }

    // Dropped items
    std::vector<DroppedItem> drops;
    int32_t dropCount;
    f.read(reinterpret_cast<char*>(&dropCount), 4);
    if (!f || dropCount < 0 || dropCount > kMaxEntries) return false;
    drops.reserve((size_t)dropCount);
    for (int i = 0; i < dropCount; i++) {
        DroppedItem d; uint8_t t; int32_t c;
        f.read(reinterpret_cast<char*>(&t), 1);
        f.read(reinterpret_cast<char*>(&c), 4);
        f.read(reinterpret_cast<char*>(&d.pos.x), 4);
        f.read(reinterpret_cast<char*>(&d.pos.y), 4);
        f.read(reinterpret_cast<char*>(&d.pos.z), 4);
        if (!f || t >= (uint8_t)ItemType::COUNT || c < 0) return false;
        d.type = (ItemType)t;
        d.count = c;
        drops.push_back(d);
    }

    // Chunks
    int32_t count;
    f.read(reinterpret_cast<char*>(&count), 4);
    if (!f || count < 0 || count > kMaxEntries) return false;

    std::unordered_map<glm::ivec2, Chunk, IVec2Hash> loaded;
    for (int i = 0; i < count; i++) {
        int32_t cx, cy;
        f.read(reinterpret_cast<char*>(&cx), 4);
        f.read(reinterpret_cast<char*>(&cy), 4);
        if (!f) return false;

        Chunk chunk;
        f.read(reinterpret_cast<char*>(chunk.tiles), sizeof(chunk.tiles));
        for (int z = 0; z < CHUNK_DEPTH; z++)
        for (int y = 0; y < CHUNK_SIZE;  y++)
        for (int x = 0; x < CHUNK_SIZE;  x++)
            f.read(reinterpret_cast<char*>(&chunk.states[z][y][x].growthStage), 1);
        for (int z = 0; z < CHUNK_DEPTH; z++)
        for (int y = 0; y < CHUNK_SIZE;  y++)
        for (int x = 0; x < CHUNK_SIZE;  x++)
            f.read(reinterpret_cast<char*>(&chunk.states[z][y][x].lastUpdatedDay), 4);
        for (int z = 0; z < CHUNK_DEPTH; z++)
        for (int y = 0; y < CHUNK_SIZE;  y++)
        for (int x = 0; x < CHUNK_SIZE;  x++) {
            uint8_t w;
            f.read(reinterpret_cast<char*>(&w), 1);
            chunk.states[z][y][x].watered = (w != 0);
        }
        if (!f) return false;

        int32_t objCount;
        f.read(reinterpret_cast<char*>(&objCount), 4);
        if (!f || objCount < 0 || objCount > kMaxEntries) return false;
        chunk.objects.clear();
        chunk.objects.reserve((size_t)objCount);
        for (int j = 0; j < objCount; j++) {
            Object o; uint8_t t;
            f.read(reinterpret_cast<char*>(&t), 1);
            f.read(reinterpret_cast<char*>(&o.pos.x), 4);
            f.read(reinterpret_cast<char*>(&o.pos.y), 4);
            f.read(reinterpret_cast<char*>(&o.pos.z), 4);
            f.read(reinterpret_cast<char*>(&o.scale), 4);
            f.read(reinterpret_cast<char*>(&o.rot), 4);
            if (!f || t >= (uint8_t)ObjectType::COUNT) return false;
            o.type = (ObjectType)t;
            chunk.objects.push_back(o);
        }

        chunk.modified = true;
        chunk.dirty    = true;
        loaded[{cx, cy}] = std::move(chunk);
    }

    // All reads succeeded — commit.
    outPlayerPos       = playerPos;
    outGameTime        = gameTime;
    outInventory       = inventory;
    outDrops           = std::move(drops);
    m_modifiedUnloaded = std::move(loaded);
    return true;
}

// ---- Growth ----
// Water gates growth: wheat advances one stage per in-game day only if the
// farmland below it was watered. Farmland then dries out (must re-water daily).

void World::growthTick(int currentDay) {
    // Applied to both loaded (m_chunks) and modified-unloaded chunks so crops
    // progress consistently whether or not the player is nearby. Growth is
    // water-gated (one stage per watered day, farmland dries daily), so an
    // unloaded chunk advances at most one stage per watering — no day-delta
    // catch-up needed (re-watering can't happen while the player is away).
    auto tickChunk = [&](Chunk& chunk) {
        bool changed = false;

        // Grow wheat sitting on watered farmland
        for (int z  = 1; z  < CHUNK_DEPTH; z++)
        for (int ly = 0; ly < CHUNK_SIZE;  ly++)
        for (int lx = 0; lx < CHUNK_SIZE;  lx++) {
            if (chunk.tiles[z][ly][lx]   != TileType::WHEAT)    continue;
            if (chunk.tiles[z-1][ly][lx] != TileType::FARMLAND) continue;
            if (!chunk.states[z-1][ly][lx].watered)             continue;
            TileState& s = chunk.states[z][ly][lx];
            if (s.growthStage < 3) {
                s.growthStage++;
                s.lastUpdatedDay = (uint32_t)currentDay;
                changed = true;
            }
        }

        // Farmland dries out each day — must be re-watered
        for (int z  = 0; z  < CHUNK_DEPTH; z++)
        for (int ly = 0; ly < CHUNK_SIZE;  ly++)
        for (int lx = 0; lx < CHUNK_SIZE;  lx++) {
            if (chunk.tiles[z][ly][lx] == TileType::FARMLAND && chunk.states[z][ly][lx].watered) {
                chunk.states[z][ly][lx].watered = false;
                changed = true; // wet -> dry visual change
            }
        }

        if (changed) chunk.dirty = true;
    };

    for (auto& [coord, chunk] : m_chunks)           tickChunk(chunk);
    for (auto& [coord, chunk] : m_modifiedUnloaded) tickChunk(chunk);
}

World::HarvestResult World::tryHarvestObject(int x, int y, ItemType tool,
                                             glm::vec3& outPos, ItemType& outDrop, int& outCount) {
    auto cc = chunkCoord(x, y);
    auto it = m_chunks.find(cc);
    if (it == m_chunks.end()) return HarvestResult::NoObject;
    Chunk& chunk = it->second;

    // Object positions are exact integer tile coords stored as floats.
    for (size_t i = 0; i < chunk.objects.size(); i++) {
        const Object& o = chunk.objects[i];
        if ((int)o.pos.x != x || (int)o.pos.y != y) continue;

        const ObjectDef& def = objectDef(o.type);
        // Player-placed structures are removable by hand; natural props need their tool.
        if (!def.placeable && def.harvestTool != tool) return HarvestResult::WrongTool;

        outPos   = o.pos;
        outDrop  = def.dropItem;
        outCount = def.dropCount;
        chunk.objects.erase(chunk.objects.begin() + i);
        chunk.dirty        = true;
        chunk.objectsDirty = true;
        chunk.grassDirty   = true;
        chunk.modified     = true; // keep the harvest across in-session unload/reload
        return HarvestResult::Harvested;
    }
    return HarvestResult::NoObject;
}

bool World::hasObjectAt(int x, int y) const {
    auto it = m_chunks.find(chunkCoord(x, y));
    if (it == m_chunks.end()) return false;
    for (const Object& o : it->second.objects)
        if ((int)o.pos.x == x && (int)o.pos.y == y) return true;
    return false;
}

bool World::isCollidableAt(int x, int y) const {
    auto it = m_chunks.find(chunkCoord(x, y));
    if (it == m_chunks.end()) return false;
    for (const Object& o : it->second.objects)
        if ((int)o.pos.x == x && (int)o.pos.y == y && objectDef(o.type).collidable)
            return true;
    return false;
}

bool World::isObjectTypeNear(int x, int y, ObjectType type, int radius) const {
    for (int dy = -radius; dy <= radius; dy++)
    for (int dx = -radius; dx <= radius; dx++) {
        const int wx = x + dx, wy = y + dy;
        auto it = m_chunks.find(chunkCoord(wx, wy));
        if (it == m_chunks.end()) continue;
        for (const Object& o : it->second.objects)
            if (o.type == type && (int)o.pos.x == wx && (int)o.pos.y == wy) return true;
    }
    return false;
}

bool World::placeObject(int x, int y, ObjectType type) {
    if (hasObjectAt(x, y)) return false;

    // Find the topmost solid tile to sit on.
    int z = -1;
    for (int zz = CHUNK_DEPTH - 1; zz >= 0; zz--) {
        if (getTile(x, y, zz) != TileType::AIR) { z = zz; break; }
    }
    if (z < 0) return false;
    if (getTile(x, y, z) == TileType::WATER) return false;

    auto it = m_chunks.find(chunkCoord(x, y));
    if (it == m_chunks.end()) return false;
    Chunk& chunk = it->second;

    Object o;
    o.pos   = { (float)x, (float)y, (float)z + 0.5f };
    o.scale = 1.0f;
    o.rot   = 0.0f;
    o.type  = type;
    chunk.objects.push_back(o);
    chunk.dirty        = true;  // triggers buildChunkBuffer → rebuilds the object buffers
    chunk.objectsDirty = true;
    chunk.grassDirty   = true;
    chunk.modified     = true;
    return true;
}

bool World::inBounds(int x, int y, int z) const {
    return z >= 0 && z < CHUNK_DEPTH;
}

bool World::isWalkable(int x, int y, int z) const {
    // Ocean base: the sea surface is traversable — the avatar (placeholder ship) sails
    // on water. Solid ground (future islands) is also walkable; AIR/crops are not.
    TileType t = getTile(x, y, z);
    return t != TileType::AIR && t != TileType::WHEAT;
}

glm::ivec3 World::worldToTile(const glm::vec3& position) const {
    return {
        static_cast<int>(std::floor(position.x + 0.5f)),
        static_cast<int>(std::floor(position.y + 0.5f)),
        static_cast<int>(std::round(position.z)) - 1
    };
}

glm::vec3 World::tileCenter(int x, int y, int z) const {
    return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
}

glm::vec3 World::tileColor(TileType type, uint8_t growthStage) {
    if (type == TileType::WHEAT) {
        static const glm::vec3 kWheatColors[4] = {
            {0.58f, 0.75f, 0.32f},  // stage 0: pale green sprout
            {0.70f, 0.78f, 0.22f},  // stage 1: yellow-green
            {0.85f, 0.78f, 0.12f},  // stage 2: yellow
            {0.95f, 0.78f, 0.05f},  // stage 3: golden
        };
        return kWheatColors[growthStage < 4 ? growthStage : 3];
    }
    return kTileColors[(int)type];
}

static const glm::vec3 kTileSideColors[] = {
    {0.0f,  0.0f,  0.0f },  // AIR
    {0.45f, 0.28f, 0.12f},  // GRASS
    {0.38f, 0.22f, 0.08f},  // DIRT
    {0.15f, 0.35f, 0.60f},  // WATER
    {0.38f, 0.38f, 0.38f},  // STONE
    {0.34f, 0.22f, 0.11f},  // WOOD
    {0.24f, 0.45f, 0.20f},  // LEAVES
    {0.22f, 0.12f, 0.05f},  // FARMLAND
    {0.50f, 0.38f, 0.15f},  // WHEAT (straw)
};

glm::vec3 World::tileSideColor(TileType type) {
    return kTileSideColors[(int)type];
}
