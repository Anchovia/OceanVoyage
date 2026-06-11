#pragma once
#include <glm/glm.hpp>

#include <vector>

static constexpr float DAY_DURATION = 120.0f; // seconds per in-game day

struct PlayerInput {
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    double mouseX = 0.0;
    double mouseY = 0.0;
    bool leftClick       = false;
    bool rightClick      = false;
    bool quit            = false;  // ESC (app-level pause toggle)
    bool rotateLeft      = false;  // Q
    bool rotateRight     = false;  // E
    bool saveKey         = false;  // Ctrl+S (raw; main edge-detects)
    bool dockKey         = false;  // Enter (raw; GameState edge-detects docking)
    bool menuUp          = false;  // Up arrow (market row select)
    bool menuDown        = false;  // Down arrow
    bool buyKey          = false;  // B (market buy 1)
    bool sellKey         = false;  // S without Ctrl (market sell 1; harmless while docked)
    bool toggleDevUi     = false;  // F3 (dev builds; main edge-detects)
    int  scrollDelta = 0;   // scroll wheel steps (camera zoom)
    int windowWidth = 1280;
    int windowHeight = 720;
    float moveSpeedMultiplier = 1.0f; // dev/test multiplier; 1.0 keeps normal speed
};

// Sailing state for the hero ship. This is the source of truth for the ship's
// position/heading: it moves with inertia and a turn radius driven by throttle
// and rudder.
struct ShipState {
    glm::vec2 position{15.0f, 15.0f};
    glm::vec2 velocity{0.0f, 0.0f};
    float heading  = -1.5707963f; // radians (-pi/2 = facing -Y)
    float yawRate  = 0.0f;
    float throttle = 0.0f;        // -1..1 forward/reverse demand
    float rudder   = 0.0f;        // -1..1 port/starboard demand
};

// Tradeable industrial-era goods (first pass, hardcoded set).
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
// list with a static market. Grows toward OceanWorld in Phase 5.
struct Port {
    int         id;
    const char* name;   // uppercase A-Z (the vector-font HUD has no lowercase)
    glm::vec2   position;
    float       radius = 30.0f; // "near port" / docking range in world metres
    std::vector<MarketEntry> market;
};

struct CargoStack {
    CargoGoodId good;
    int         count;
};

// Ship cargo hold: plain unit-count capacity. Weight-based loading arrives
// with ShipDef in Phase 6.
struct CargoHold {
    int capacity = 100;
    std::vector<CargoStack> stacks;

    int used() const {
        int total = 0;
        for (const CargoStack& s : stacks) total += s.count;
        return total;
    }
};

// Gameplay mode, independent of the app flow (menu/settings/pause).
// Sailing = free movement on the open sea; Docked = anchored at a port with
// the port menu open (set sail / trade).
enum class GameMode { Sailing, Docked };

class GameState {
public:
    void update(float dt, const PlayerInput& input);

    const ShipState& ship() const { return m_ship; }

    // Ship position as a world-space point for the camera target and renderer
    // (shadow center). The fixed deck height only feeds those consumers;
    // buoyancy recomputes the visual height from the FFT readback.
    glm::vec3 shipWorldPosition() const {
        constexpr float kShipDeckZ = 1.0f;
        return { m_ship.position.x, m_ship.position.y, kShipDeckZ };
    }

    const std::vector<Port>& ports() const { return m_ports; }
    // Nearest port to the ship, with distance and a normalized world-space
    // direction toward it. Returns nullptr only if no ports exist.
    const Port* nearestPort(float& outDistance, glm::vec2& outDir) const;

    GameMode mode() const { return m_mode; }
    // True while sailing inside a port's radius slowly enough to dock.
    bool canDock() const;
    // Port the ship is docked at (nullptr while sailing).
    const Port* dockedPort() const {
        return (m_mode == GameMode::Docked) ? &m_ports[(size_t)m_dockedPortIndex] : nullptr;
    }
    // Leaves the port menu and returns to sailing (ship starts at rest).
    void setSail() { m_mode = GameMode::Sailing; m_dockedPortIndex = -1; m_marketOpen = false; }

    // Market screen (sub-state of Docked, opened from the port menu).
    bool marketOpen() const { return m_marketOpen; }
    void openMarket() {
        if (m_mode == GameMode::Docked) { m_marketOpen = true; m_marketSelected = 0; }
    }
    void closeMarket() { m_marketOpen = false; }
    int  marketSelected() const { return m_marketSelected; }

    const CargoHold& cargo() const { return m_cargo; }
    int cargoCount(CargoGoodId good) const;
    int money() const { return m_money; }

    int   day()       const { return m_day; }
    float timeOfDay() const { return m_timeOfDay; } // 0.0=midnight, 0.5=noon, 1.0=midnight
    float time()      const { return m_time; }

    // Restores the full sailing state (position/velocity/heading/yawRate/
    // throttle/rudder) from a save.
    void setShipState(const ShipState& s) { m_ship = s; }
    void setTime(float t);
    void setMoney(int m) { m_money = m; }
    void setCargo(const CargoHold& c) { m_cargo = c; }

private:
    // Integrates the ship's sailing physics from WASD (throttle/rudder) input.
    void updateShipPhysics(float dt, const PlayerInput& input);

    // Market row selection + buy/sell for the docked port (edge-detected keys).
    void updateMarket(const PlayerInput& input);

    void cargoAdd(CargoGoodId good, int count);     // merges into an existing stack
    bool cargoRemove(CargoGoodId good, int count);  // false if not enough held

    ShipState m_ship;

    GameMode m_mode            = GameMode::Sailing;
    int      m_dockedPortIndex = -1;
    bool     m_prevDockKey     = false; // edge-detect for the dock key

    bool m_marketOpen     = false;
    int  m_marketSelected = 0;
    bool m_prevMenuUp     = false;
    bool m_prevMenuDown   = false;
    bool m_prevBuyKey     = false;
    bool m_prevSellKey    = false;

    // BRISTOL sits ~200 m ahead of the initial ship heading (-Y); LIVERPOOL is
    // ~600 m east of it. Becomes data-driven with OceanWorld (Phase 5).
    // Market prices are static for now (demand/supply pricing arrives Phase 6),
    // but differentiated so both directions have a profitable lane:
    // coal east (BRISTOL 8 -> LIVERPOOL 11), machinery west (LIVERPOOL 44 -> BRISTOL 48).
    std::vector<Port> m_ports{
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
    };

    CargoHold m_cargo;
    int       m_money = 1000;

    float m_time      = 0.0f;
    int   m_day       = 0;
    int   m_prevDay   = -1;
    float m_timeOfDay = 0.0f;
};
