#include "game/OceanWorld.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

// Four ports with type-driven market profiles. Prices are static (dynamic
// demand/supply arrives Phase 6) but differentiated so several lanes are
// profitable without one dominating (buy at source < sell at destination):
//   Coal:      CARDIFF 5  -> GLASGOW 13 (+8, longest lane) / LIVERPOOL 11 (+6)
//   Steel:     LIVERPOOL 26 -> GLASGOW 34 (+8)
//   Machinery: LIVERPOOL 44 -> CARDIFF 52 (+8) / BRISTOL 48 (+4)
//   Grain:     GLASGOW 9  -> CARDIFF 12 (+3) / BRISTOL 10 -> CARDIFF (+2)
//   IronOre:   BRISTOL 12 -> GLASGOW 15 (+3)
// Same-port buy/sell stays a guaranteed loss (buy > sell everywhere).
// Geography: BRISTOL/LIVERPOOL frame the island strait lane (y = -185);
// CARDIFF sits south past the strait, GLASGOW north across open water.
OceanWorld::OceanWorld()
    : m_ports{
        { 0, "BRISTOL", PortType::Trade, {15.0f, -185.0f}, 30.0f,
          { { CargoGoodId::Coal,       8,  6, 200 },
            { CargoGoodId::IronOre,   12,  9, 150 },
            { CargoGoodId::Steel,     30, 24,  60 },
            { CargoGoodId::Machinery, 60, 48,  25 },
            { CargoGoodId::Grain,     10,  8, 180 } } },
        { 1, "LIVERPOOL", PortType::Industrial, {615.0f, -185.0f}, 30.0f,
          { { CargoGoodId::Coal,      14, 11,  40 },
            { CargoGoodId::IronOre,   16, 13,  80 },
            { CargoGoodId::Steel,     26, 21,  90 },
            { CargoGoodId::Machinery, 44, 38,  60 },
            { CargoGoodId::Grain,     13, 11,  70 } } },
        { 2, "CARDIFF", PortType::Coal, {340.0f, -430.0f}, 30.0f,
          { { CargoGoodId::Coal,       5,  4, 400 },
            { CargoGoodId::IronOre,   14, 11,  60 },
            { CargoGoodId::Steel,     33, 27,  30 },
            { CargoGoodId::Machinery, 64, 52,  15 },
            { CargoGoodId::Grain,     14, 12,  40 } } },
        { 3, "GLASGOW", PortType::Shipyard, {380.0f, 120.0f}, 30.0f,
          { { CargoGoodId::Coal,      16, 13,  30 },
            { CargoGoodId::IronOre,   18, 15,  50 },
            { CargoGoodId::Steel,     40, 34,  25 },
            { CargoGoodId::Machinery, 62, 50,  20 },
            { CargoGoodId::Grain,      9,  7, 150 } } },
    }
    // A mid-route pair forming a navigable strait (~50 m) across the
    // BRISTOL-LIVERPOOL lane (y = -185), plus a scenery island south of
    // BRISTOL. Radii are waterline ellipse half-axes in world metres.
    , m_islands{
        { {300.0f, -110.0f}, 85.0f, 50.0f,  0.45f },
        { {335.0f, -265.0f}, 65.0f, 42.0f, -0.35f },
        { {110.0f, -340.0f}, 50.0f, 80.0f,  1.20f },
    } {}

Wind OceanWorld::windAt(float gameTime) const {
    // Two incommensurate sine pairs give a wandering-but-smooth drift:
    // direction swings over ~10-25 minute cycles, speed breathes a little
    // faster. Stateless on purpose (see header).
    const float angle = 0.85f
        + 0.55f * std::sin(gameTime * 0.0041f)
        + 0.30f * std::sin(gameTime * 0.0173f + 1.7f);
    const float speed = 6.5f
        + 2.4f * std::sin(gameTime * 0.0067f + 0.9f)
        + 1.1f * std::sin(gameTime * 0.0231f + 2.3f);
    return { { std::cos(angle), std::sin(angle) }, std::max(speed, 0.5f) };
}

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
