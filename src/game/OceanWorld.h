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

// Port character: drives the market profile for now (a Coal port mines cheap
// coal, a Shipyard pays well for steel). Facilities (repair, ship purchase)
// attach to these types in Phase 6.
enum class PortType : uint8_t { Trade = 0, Industrial, Coal, Shipyard };

inline const char* portTypeName(PortType t) {
    switch (t) {
        case PortType::Trade:      return "TRADE PORT";
        case PortType::Industrial: return "INDUSTRIAL PORT";
        case PortType::Coal:       return "COAL PORT";
        case PortType::Shipyard:   return "SHIPYARD";
        default:                   return "";
    }
}

// A trade port on the open sea. Hardcoded list with a static market;
// dynamic demand/supply pricing arrives in Phase 6.
struct Port {
    int         id;
    const char* name;   // uppercase A-Z (the vector-font HUD has no lowercase)
    PortType    type;
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

// Global wind state at a moment in time. direction is the normalized world
// vector the wind blows TOWARD (the HUD displays the nautical "from"
// direction). speed is in m/s.
struct Wind {
    glm::vec2 direction;
    float     speed;
};

// World state for the open sea: the home of the geography the ship sails
// through. Residents so far: ports, islands, and wind; routes join in a
// later Phase 5 slice. Knows nothing about the renderer — mesh instances are
// produced in a separate transform step (main.cpp builds PortRenderInstance
// from ports()).
class OceanWorld {
public:
    OceanWorld();

    const std::vector<Port>& ports() const { return m_ports; }
    // Mutable access for market trades (stock changes when the player buys/sells).
    Port& portAt(size_t index) { return m_ports[index]; }

    const std::vector<Island>& islands() const { return m_islands; }

    // Deterministic global wind: a slow minutes-scale drift computed purely
    // from gameTime, so saves reproduce it for free and Phase 7 sail physics
    // can promote it without a format change.
    Wind windAt(float gameTime) const;

    // Nearest port to `position`, with distance and a normalized world-space
    // direction toward it. Returns nullptr only if no ports exist.
    const Port* nearestPort(glm::vec2 position, float& outDistance, glm::vec2& outDir) const;

private:
    std::vector<Port>   m_ports;
    std::vector<Island> m_islands;
};
