#include "game/OceanWorld.h"

#include <glm/geometric.hpp>

// BRISTOL sits ~200 m ahead of the initial ship heading (-Y); LIVERPOOL is
// ~600 m east of it. Becomes data-driven with the Phase 5 port expansion.
// Market prices are static for now (demand/supply pricing arrives Phase 6),
// but differentiated so both directions have a profitable lane:
// coal east (BRISTOL 8 -> LIVERPOOL 11), machinery west (LIVERPOOL 44 -> BRISTOL 48).
OceanWorld::OceanWorld()
    : m_ports{
        { 0, "BRISTOL", {15.0f, -185.0f}, 30.0f,
          { { CargoGoodId::Coal,       8,  6, 200 },
            { CargoGoodId::IronOre,   12,  9, 150 },
            { CargoGoodId::Steel,     30, 24,  60 },
            { CargoGoodId::Machinery, 60, 48,  25 },
            { CargoGoodId::Grain,     10,  8, 180 } } },
        { 1, "LIVERPOOL", {615.0f, -185.0f}, 30.0f,
          { { CargoGoodId::Coal,      14, 11,  40 },
            { CargoGoodId::IronOre,   16, 13,  80 },
            { CargoGoodId::Steel,     26, 21,  90 },
            { CargoGoodId::Machinery, 44, 38,  60 },
            { CargoGoodId::Grain,     13, 11,  70 } } },
    }
    // A mid-route pair forming a navigable strait (~50 m) across the
    // BRISTOL-LIVERPOOL lane (y = -185), plus a scenery island south of
    // BRISTOL. Radii are waterline ellipse half-axes in world metres.
    , m_islands{
        { {300.0f, -110.0f}, 85.0f, 50.0f,  0.45f },
        { {335.0f, -265.0f}, 65.0f, 42.0f, -0.35f },
        { {110.0f, -340.0f}, 50.0f, 80.0f,  1.20f },
    } {}

const Port* OceanWorld::nearestPort(glm::vec2 position, float& outDistance, glm::vec2& outDir) const {
    const Port* best = nullptr;
    float bestDist = 0.0f;
    for (const Port& p : m_ports) {
        const float d = glm::length(p.position - position);
        if (!best || d < bestDist) {
            best = &p;
            bestDist = d;
        }
    }
    if (!best) return nullptr;
    outDistance = bestDist;
    outDir = (bestDist > 0.001f) ? (best->position - position) / bestDist
                                 : glm::vec2(0.0f);
    return best;
}
