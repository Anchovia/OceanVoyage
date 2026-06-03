#pragma once
#include "world/Chunk.h"

class TerrainGen {
public:
    static void generate(int cx, int cy, Chunk& chunk);

private:
    static float hash(int x, int y);
    static float valueNoise(float x, float y);
    static float fbm(float x, float y);
    static void  placeTrees(int cx, int cy, Chunk& chunk);
    static void  placeRocks(int cx, int cy, Chunk& chunk);
};
