#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Tradeable industrial-era goods (first pass, hardcoded set). Lives with the
// world/market data for now; moves to dedicated trade-good defs in Phase 6.
enum class CargoGoodId : uint8_t { Coal = 0, IronOre, Steel, Machinery, Grain, COUNT };

inline const char* cargoGoodName(CargoGoodId g) {
    switch (g) {
        case CargoGoodId::Coal:      return "COAL";
        case CargoGoodId::IronOre:   return "IRON ORE";
        case CargoGoodId::Steel:     return "STEEL";
        case CargoGoodId::Machinery: return "MACHINERY";
        case CargoGoodId::Grain:     return "GRAIN";
        default:                     return "";
    }
}

// One market row at a port. buyPrice is what the player pays, sellPrice what
// the player receives — the spread makes same-port buy/sell a guaranteed loss.
struct MarketEntry {
    CargoGoodId good;
    int         buyPrice;
    int         sellPrice;
    int         stock;     // port-side inventory available to buy
};

// A trade port on the open sea. First-pass data (ROADMAP 3b/3c): hardcoded
// list with a static market. Port types and per-type markets arrive with the
// Phase 5 port expansion.
struct Port {
    int         id;
    const char* name;   // uppercase A-Z (the vector-font HUD has no lowercase)
    glm::vec2   position;
    float       radius = 30.0f; // "near port" / docking range in world metres
    std::vector<MarketEntry> market;
};

// A landmass on the open sea. The footprint is an ellipse at the waterline
// (center / half-axes / rotation); ship collision resolves against the
// inflated ellipse distance — no tile grid. Render geometry is baked from the
// same parameters in a separate transform step (main.cpp ->
// VulkanContext::setIslands).
struct Island {
    glm::vec2 center;
    float     radiusX;
    float     radiusY;
    float     rotation; // radians around +Z
};

// World state for the open sea: the home of the geography the ship sails
// through. Residents so far: ports and islands; wind and routes join in
// later Phase 5 slices. Knows nothing about the renderer — mesh instances are
// produced in a separate transform step (main.cpp builds PortRenderInstance
// from ports()).
class OceanWorld {
public:
    OceanWorld();

    const std::vector<Port>& ports() const { return m_ports; }
    // Mutable access for market trades (stock changes when the player buys/sells).
    Port& portAt(size_t index) { return m_ports[index]; }

    const std::vector<Island>& islands() const { return m_islands; }

    // Nearest port to `position`, with distance and a normalized world-space
    // direction toward it. Returns nullptr only if no ports exist.
    const Port* nearestPort(glm::vec2 position, float& outDistance, glm::vec2& outDir) const;

private:
    std::vector<Port>   m_ports;
    std::vector<Island> m_islands;
};
