#include "TerrainGen.h"
#include <cmath>

float TerrainGen::hash(int x, int y) {
    unsigned int h = (unsigned int)((x * 374761393) ^ (y * 668265263));
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu);
}

float TerrainGen::valueNoise(float x, float y) {
    int   ix = static_cast<int>(std::floor(x));
    int   iy = static_cast<int>(std::floor(y));
    float fx = x - ix;
    float fy = y - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float v00 = hash(ix,     iy);
    float v10 = hash(ix + 1, iy);
    float v01 = hash(ix,     iy + 1);
    float v11 = hash(ix + 1, iy + 1);

    return (v00 * (1.0f - ux) + v10 * ux) * (1.0f - uy)
         + (v01 * (1.0f - ux) + v11 * ux) * uy;
}

float TerrainGen::fbm(float x, float y) {
    float value     = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    for (int i = 0; i < 4; i++) {
        value     += valueNoise(x * frequency, y * frequency) * amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value;
}

static constexpr float HEIGHT_SCALE = 1.0f / 32.0f;
static constexpr float BIOME_SCALE  = 1.0f / 24.0f;

void TerrainGen::generate(int cx, int cy, Chunk& chunk) {
    // OceanVoyage: flat sea base. Fill the ground layer (z=0) with water so the chunk
    // mesher emits a flat water surface (top face — z=1 above is AIR). Islands, coastal
    // terrain, and the dedicated ocean surface come in later passes. The farm
    // height/biome generator (fbm) and prop placement (placeTrees/placeRocks) are kept
    // below as reference until the ocean world generator replaces them.
    (void)cx;
    (void)cy;
    for (int ly = 0; ly < CHUNK_SIZE; ly++)
        for (int lx = 0; lx < CHUNK_SIZE; lx++)
            chunk.tiles[0][ly][lx] = TileType::WATER;

    chunk.dirty = true;
}

void TerrainGen::placeTrees(int cx, int cy, Chunk& chunk) {
    for (int ly = 0; ly < CHUNK_SIZE; ly++) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            // Only on flat grass with open sky above
            if (chunk.tiles[0][ly][lx] != TileType::GRASS) continue;
            if (chunk.tiles[1][ly][lx] != TileType::AIR)    continue;

            const int wx = cx * CHUNK_SIZE + lx;
            const int wy = cy * CHUNK_SIZE + ly;

            const float b = fbm(wx * BIOME_SCALE + 100.0f, wy * BIOME_SCALE + 100.0f);
            if (b < 0.58f) continue; // forest biome only

            if (hash(wx + 7000, wy + 7000) < 0.90f) continue; // sparse

            // Tree object sits on top of the Z=0 block (top face at Z=0.5)
            Object tree;
            tree.pos   = { (float)wx, (float)wy, 0.5f };
            tree.scale = 0.8f + hash(wx + 11000, wy + 11000) * 0.5f;  // 0.8 .. 1.3
            tree.rot   = hash(wx + 13000, wy + 13000) * 6.2831853f;   // 0 .. 2pi
            tree.type  = ObjectType::TREE;
            chunk.objects.push_back(tree);
        }
    }
}

void TerrainGen::placeRocks(int cx, int cy, Chunk& chunk) {
    for (int ly = 0; ly < CHUNK_SIZE; ly++) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            // Only on flat ground with open sky above
            const TileType ground = chunk.tiles[0][ly][lx];
            if (ground != TileType::GRASS && ground != TileType::DIRT) continue;
            if (chunk.tiles[1][ly][lx] != TileType::AIR) continue;

            const int wx = cx * CHUNK_SIZE + lx;
            const int wy = cy * CHUNK_SIZE + ly;

            const float b = fbm(wx * BIOME_SCALE + 100.0f, wy * BIOME_SCALE + 100.0f);
            if (b > 0.45f) continue; // open / dry areas, away from the forest biome

            if (hash(wx + 21000, wy + 21000) < 0.94f) continue; // sparse

            Object rock;
            rock.pos   = { (float)wx, (float)wy, 0.5f };
            rock.scale = 0.6f + hash(wx + 23000, wy + 23000) * 0.5f; // 0.6 .. 1.1
            rock.rot   = hash(wx + 25000, wy + 25000) * 6.2831853f;  // 0 .. 2pi
            rock.type  = ObjectType::ROCK;
            chunk.objects.push_back(rock);
        }
    }
}
