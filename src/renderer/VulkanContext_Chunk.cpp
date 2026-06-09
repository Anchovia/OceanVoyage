#include "VulkanContext.h"
#include "renderer/Types.h"
#include "world/World.h"

#include <array>
#include <cstring>
#include <utility>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

// ============================================================
//  Chunk voxel mesh builder
// ============================================================
void VulkanContext::buildChunkBuffer(const glm::ivec2& coord, Chunk& chunk) {
    // 6 face local vertex offsets, normals, neighbor offsets, top-face flag
    struct FaceDef {
        glm::vec3  verts[4];
        glm::vec3  normal;
        glm::ivec3 neighborOff;
        bool       isTop;
    };
    static const FaceDef kFaces[6] = {
        // Top (+Z)
        {{{-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}}, {0,0,1},  {0,0,1},  true  },
        // Bottom (-Z)
        {{{-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}}, {0,0,-1}, {0,0,-1}, false },
        // Front (+Y)
        {{{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f}},  {0,1,0},  {0,1,0},  false },
        // Back (-Y)
        {{{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{-0.5f,-0.5f,0.5f}}, {0,-1,0}, {0,-1,0}, false },
        // Right (+X)
        {{{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f},{0.5f,-0.5f,0.5f}},  {1,0,0},  {1,0,0},  false },
        // Left (-X)
        {{{-0.5f,0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f}}, {-1,0,0}, {-1,0,0}, false },
    };

    const int baseX = coord.x * CHUNK_SIZE;
    const int baseY = coord.y * CHUNK_SIZE;

    std::vector<ChunkVertex> vertices;
    std::vector<uint32_t>    indices;

    // Padded neighborhood copy (chunk + 1-tile border) so face-cull and AO
    // sampling use array indexing instead of per-vertex hashmap lookups.
    // Interior comes from chunk.tiles directly; only the border ring hits getTile.
    static TileType N[CHUNK_DEPTH + 2][CHUNK_SIZE + 2][CHUNK_SIZE + 2];
    for (int lz = -1; lz <= CHUNK_DEPTH; lz++)
    for (int ly = -1; ly <= CHUNK_SIZE; ly++)
    for (int lx = -1; lx <= CHUNK_SIZE; lx++) {
        bool inside = lx >= 0 && lx < CHUNK_SIZE && ly >= 0 && ly < CHUNK_SIZE && lz >= 0 && lz < CHUNK_DEPTH;
        N[lz + 1][ly + 1][lx + 1] = inside
            ? chunk.tiles[lz][ly][lx]
            : m_world.getTile(baseX + lx, baseY + ly, lz);
    }
    auto Nat = [&](int lx, int ly, int lz) -> TileType {
        return N[lz + 1][ly + 1][lx + 1];
    };

    // Per-vertex ambient occlusion, sampling the padded buffer.
    auto vertexAO = [&Nat](int tlx, int tly, int tz, const glm::vec3& n, const glm::vec3& lp) -> float {
        glm::ivec3 ni((int)n.x, (int)n.y, (int)n.z);
        int axes[2], na = 0;
        for (int a = 0; a < 3; a++) if (ni[a] == 0) axes[na++] = a;
        glm::ivec3 d0(0), d1(0);
        d0[axes[0]] = (lp[axes[0]] > 0.0f) ? 1 : -1;
        d1[axes[1]] = (lp[axes[1]] > 0.0f) ? 1 : -1;
        glm::ivec3 b(tlx + ni.x, tly + ni.y, tz + ni.z);
        auto sol = [&](glm::ivec3 p) {
            TileType t = Nat(p.x, p.y, p.z);
            return (t != TileType::AIR && t != TileType::WATER) ? 1 : 0;
        };
        int s0 = sol(b + d0), s1 = sol(b + d1), c = sol(b + d0 + d1);
        int ao = (s0 && s1) ? 0 : (3 - (s0 + s1 + c));
        static const float levels[4] = { 0.5f, 0.7f, 0.85f, 1.0f };
        return levels[ao];
    };

    for (int z  = 0; z  < CHUNK_DEPTH; z++)
    for (int ly = 0; ly < CHUNK_SIZE;  ly++)
    for (int lx = 0; lx < CHUNK_SIZE;  lx++) {
        TileType t = chunk.tiles[z][ly][lx];
        // WATER is drawn by the dedicated Gerstner ocean surface, not as voxels. Keep the
        // tiles in the data model (collision / walkability) but emit no chunk geometry.
        if (t == TileType::AIR || t == TileType::WATER) continue;

        const int wx = baseX + lx;
        const int wy = baseY + ly;
        const uint8_t   growthStage = chunk.states[z][ly][lx].growthStage;
        glm::vec3       topColor    = World::tileColor(t, growthStage);
        glm::vec3       sideColor   = World::tileSideColor(t);
        // Watered farmland renders darker (moist)
        if (t == TileType::FARMLAND && chunk.states[z][ly][lx].watered) {
            topColor  *= 0.6f;
            sideColor *= 0.6f;
        }
        const glm::vec3 center      = { (float)wx, (float)wy, (float)z };

        for (const auto& face : kFaces) {
            // Skip if neighbor tile is opaque
            TileType neighbor = Nat(lx + face.neighborOff.x,
                                    ly + face.neighborOff.y,
                                    z  + face.neighborOff.z);
            if (neighbor != TileType::AIR) continue;

            const glm::vec3 color = face.isTop ? topColor : sideColor;
            const float     layer = (float)tileFaceLayer(t, face.isTop);
            const uint32_t  base  = (uint32_t)vertices.size();

            // Per-face UV matching the 4 corner order of FaceDef::verts.
            static const glm::vec2 faceUV[4] = { {0,0}, {1,0}, {1,1}, {0,1} };
            for (int i = 0; i < 4; i++) {
                float ao = vertexAO(lx, ly, z, face.normal, face.verts[i]);
                vertices.push_back({ center + face.verts[i], face.normal, color * ao, faceUV[i], layer });
            }

            indices.insert(indices.end(), {
                base+0, base+1, base+2,
                base+0, base+2, base+3
            });
        }
    }

    auto& data = m_chunkBuffers[coord];

    // Defer destruction of old buffers — GPU may still be reading them (move nulls them)
    deferDestroy(std::move(data.vertexBuffer));
    deferDestroy(std::move(data.indexBuffer));

    data.indexCount = (uint32_t)indices.size();
    if (data.indexCount == 0) return;

    VkDeviceSize vSize = sizeof(ChunkVertex) * vertices.size();
    data.vertexBuffer = createBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* vMapped;
    vkCheck(vkMapMemory(m_device, data.vertexBuffer.memory, 0, vSize, 0, &vMapped),
        "Failed to map chunk vertex buffer");
    memcpy(vMapped, vertices.data(), vSize);
    vkUnmapMemory(m_device, data.vertexBuffer.memory);

    VkDeviceSize iSize = sizeof(uint32_t) * indices.size();
    data.indexBuffer = createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* iMapped;
    vkCheck(vkMapMemory(m_device, data.indexBuffer.memory, 0, iSize, 0, &iMapped),
        "Failed to map chunk index buffer");
    memcpy(iMapped, indices.data(), iSize);
    vkUnmapMemory(m_device, data.indexBuffer.memory);

    // Visual dressing instances change only when terrain/open sky or blocking objects change.
    if (chunk.grassDirty) {
        buildGroundDressingBuffer(coord, chunk);
        chunk.grassDirty = false;
    }
    if (chunk.objectsDirty) {
        buildChunkObjectBuffer(coord, chunk);
        chunk.objectsDirty = false;
    }
}

// ============================================================
//  Per-chunk visual ground dressing
// ============================================================
void VulkanContext::buildGroundDressingBuffer(const glm::ivec2& coord, Chunk& chunk) {
    auto& data = m_chunkBuffers[coord];
    deferDestroy(std::move(data.groundPatchBuffer));
    deferDestroy(std::move(data.pebbleBuffer));
    data.groundPatchCount = 0;
    data.pebbleCount = 0;

    const int baseX = coord.x * CHUNK_SIZE;
    const int baseY = coord.y * CHUNK_SIZE;
    std::vector<ObjectInstance> patches;
    std::vector<ObjectInstance> pebbles;
    patches.reserve(64);
    pebbles.reserve(32);

    auto hash01 = [](int wx, int wy, int salt) -> float {
        uint32_t h = (uint32_t)wx * 73856093u ^ (uint32_t)wy * 19349663u ^ (uint32_t)salt * 83492791u;
        h ^= h >> 13;
        h *= 1274126177u;
        return (float)(h & 65535u) / 65535.0f;
    };
    auto floorDiv = [](int v, int d) -> int {
        return v >= 0 ? v / d : -((-v + d - 1) / d);
    };
    auto smooth = [](float t) -> float {
        return t * t * (3.0f - 2.0f * t);
    };
    auto lerp = [](float a, float b, float t) -> float {
        return a + (b - a) * t;
    };
    auto clamp01 = [](float v) -> float {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    auto valueNoise = [&](int wx, int wy, int scale, int salt) -> float {
        const int gx = floorDiv(wx, scale);
        const int gy = floorDiv(wy, scale);
        const float fx = (float)(wx - gx * scale) / (float)scale;
        const float fy = (float)(wy - gy * scale) / (float)scale;
        const float sx = smooth(fx);
        const float sy = smooth(fy);

        const float a = hash01(gx,     gy,     salt);
        const float b = hash01(gx + 1, gy,     salt);
        const float c = hash01(gx,     gy + 1, salt);
        const float d = hash01(gx + 1, gy + 1, salt);
        return lerp(lerp(a, b, sx), lerp(c, d, sx), sy);
    };
    auto hasObjectAt = [&chunk](int wx, int wy) {
        for (const Object& o : chunk.objects)
            if ((int)o.pos.x == wx && (int)o.pos.y == wy)
                return true;
        return false;
    };

    for (int z  = 0; z  < CHUNK_DEPTH - 1; z++)
    for (int ly = 0; ly < CHUNK_SIZE;      ly++)
    for (int lx = 0; lx < CHUNK_SIZE;      lx++) {
        const TileType ground = chunk.tiles[z][ly][lx];
        if (ground != TileType::GRASS && ground != TileType::DIRT) continue;
        if (chunk.tiles[z + 1][ly][lx] != TileType::AIR) continue;

        const int wx = baseX + lx;
        const int wy = baseY + ly;
        if (hasObjectAt(wx, wy)) continue;

        int openGround = 0;
        for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            const TileType t = m_world.getTile(wx + dx, wy + dy, z);
            if ((t == TileType::GRASS || t == TileType::DIRT) &&
                m_world.getTile(wx + dx, wy + dy, z + 1) == TileType::AIR)
                openGround++;
        }

        const float patchField = smooth(clamp01((valueNoise(wx, wy, 19, 211) - 0.22f) / 0.70f));
        const float openBias = 0.65f + ((float)openGround / 8.0f) * 0.35f;
        const float patchChance = (ground == TileType::DIRT ? 0.006f : 0.0f) +
                                  patchField * (ground == TileType::DIRT ? 0.014f : 0.0f);
        bool patchPlaced = false;
        if (hash01(wx, wy, 223) < patchChance * openBias) {
            const float ox = (hash01(wx, wy, 227) - 0.5f) * 0.24f;
            const float oy = (hash01(wx, wy, 229) - 0.5f) * 0.24f;
            const float sc = 0.28f + hash01(wx, wy, 233) * 0.18f;
            const float rt = hash01(wx, wy, 239) * 6.2831853f;
            patches.push_back({{(float)wx + ox, (float)wy + oy, (float)z + 0.500f}, sc, rt});
            patchPlaced = true;
        }

        const float pebbleField = 1.0f - valueNoise(wx, wy, 15, 251);
        const float pebbleChance = (ground == TileType::DIRT ? 0.006f : 0.001f) +
                                   pebbleField * (ground == TileType::DIRT ? 0.012f : 0.003f);
        if (!patchPlaced && hash01(wx, wy, 257) < pebbleChance * openBias) {
            const float ox = (hash01(wx, wy, 263) - 0.5f) * 0.38f;
            const float oy = (hash01(wx, wy, 269) - 0.5f) * 0.38f;
            const float sc = 0.20f + hash01(wx, wy, 271) * 0.16f;
            const float rt = hash01(wx, wy, 277) * 6.2831853f;
            pebbles.push_back({{(float)wx + ox, (float)wy + oy, (float)z + 0.505f}, sc, rt});
        }
    }

    if (!patches.empty()) {
        data.groundPatchCount = (uint32_t)patches.size();
        VkDeviceSize size = sizeof(ObjectInstance) * patches.size();
        data.groundPatchBuffer = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped;
        vkCheck(vkMapMemory(m_device, data.groundPatchBuffer.memory, 0, size, 0, &mapped),
            "Failed to map ground patch buffer");
        memcpy(mapped, patches.data(), size);
        vkUnmapMemory(m_device, data.groundPatchBuffer.memory);
    }

    if (!pebbles.empty()) {
        data.pebbleCount = (uint32_t)pebbles.size();
        VkDeviceSize size = sizeof(ObjectInstance) * pebbles.size();
        data.pebbleBuffer = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped;
        vkCheck(vkMapMemory(m_device, data.pebbleBuffer.memory, 0, size, 0, &mapped),
            "Failed to map pebble buffer");
        memcpy(mapped, pebbles.data(), size);
        vkUnmapMemory(m_device, data.pebbleBuffer.memory);
    }
}

// ============================================================
//  Per-chunk object (tree) instance buffer
// ============================================================
void VulkanContext::buildChunkObjectBuffer(const glm::ivec2& coord, Chunk& chunk) {
    auto& data = m_chunkBuffers[coord];

    // Release any previously built groups (GPU may still be reading them)
    for (auto& g : data.objGroups)
        deferDestroy(std::move(g.buffer));
    data.objGroups.clear();

    if (chunk.objects.empty()) return;

    // Group object instances by type — one instance buffer per type present
    std::array<std::vector<ObjectInstance>, (size_t)ObjectType::COUNT> byType;
    for (const auto& o : chunk.objects)
        byType[(size_t)o.type].push_back({ o.pos, o.scale, o.rot });

    for (size_t t = 0; t < byType.size(); t++) {
        const auto& insts = byType[t];
        if (insts.empty()) continue;

        ChunkRenderData::ObjGroup group;
        group.type  = (ObjectType)t;
        group.count = (uint32_t)insts.size();

        VkDeviceSize oSize = sizeof(ObjectInstance) * insts.size();
        group.buffer = createBuffer(oSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* oMapped;
        vkCheck(vkMapMemory(m_device, group.buffer.memory, 0, oSize, 0, &oMapped),
            "Failed to map chunk object instance buffer");
        memcpy(oMapped, insts.data(), oSize);
        vkUnmapMemory(m_device, group.buffer.memory);

        data.objGroups.push_back(std::move(group));
    }
}

// ============================================================
//  Dirty chunk rebuild (called every frame)
// ============================================================
void VulkanContext::rebuildDirtyChunks() {
    // Free GPU buffers for chunks no longer in the world (deferred)
    for (auto it = m_chunkBuffers.begin(); it != m_chunkBuffers.end(); ) {
        if (m_world.chunks().find(it->first) == m_world.chunks().end()) {
            auto& d = it->second;
            deferDestroy(std::move(d.vertexBuffer));
            deferDestroy(std::move(d.indexBuffer));
            deferDestroy(std::move(d.groundPatchBuffer));
            deferDestroy(std::move(d.pebbleBuffer));
            for (auto& g : d.objGroups)
                deferDestroy(std::move(g.buffer));
            it = m_chunkBuffers.erase(it);
        } else {
            ++it;
        }
    }

    // Rebuild dirty chunks — capped per frame to avoid a spike when many turn dirty
    // at once (chunk streaming on a boundary cross, or growthTick on a day change).
    // Remaining dirty chunks keep their flag and are handled over the next frames.
    static constexpr int MAX_CHUNK_BUILDS_PER_FRAME = 2;
    int builtThisFrame = 0;
    for (auto& [coord, chunk] : m_world.chunks()) {
        if (!chunk.dirty) continue;
        buildChunkBuffer(coord, chunk);
        chunk.dirty = false;
        if (++builtThisFrame >= MAX_CHUNK_BUILDS_PER_FRAME) break;
    }
}
