#pragma once
#include "game/Player.h"

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
    bool toggleDevUi     = false;  // F3 (dev builds; main edge-detects)
    int  scrollDelta = 0;   // scroll wheel steps (camera zoom)
    int windowWidth = 1280;
    int windowHeight = 720;
    float moveSpeedMultiplier = 1.0f; // dev/test multiplier; 1.0 keeps normal speed
};

// Sailing state for the hero ship. This is the source of truth for the ship's
// position/heading: it moves with inertia and a turn radius driven by throttle
// and rudder, instead of the old camera-relative tile walking. The farm Player
// is kept as a temporary compatibility shim that mirrors this state.
struct ShipState {
    glm::vec2 position{15.0f, 15.0f};
    glm::vec2 velocity{0.0f, 0.0f};
    float heading  = -1.5707963f; // radians; matches the initial Player facing {0,-1}
    float yawRate  = 0.0f;
    float throttle = 0.0f;        // -1..1 forward/reverse demand
    float rudder   = 0.0f;        // -1..1 port/starboard demand
};

class GameState {
public:
    void update(float dt, const PlayerInput& input);

    const Player& player() const { return m_player; }
    const ShipState& ship() const { return m_ship; }

    int   day()       const { return m_day; }
    float timeOfDay() const { return m_timeOfDay; } // 0.0=midnight, 0.5=noon, 1.0=midnight
    float time()      const { return m_time; }

    // Restores the full sailing state (position/velocity/heading/yawRate/
    // throttle/rudder) from a save and re-syncs the legacy Player mirror.
    void setShipState(const ShipState& s);
    void setTime(float t);

private:
    // Integrates the ship's sailing physics from WASD (throttle/rudder) input.
    void updateShipPhysics(float dt, const PlayerInput& input);

    Player    m_player;
    ShipState m_ship;

    float m_time      = 0.0f;
    int   m_day       = 0;
    int   m_prevDay   = -1;
    float m_timeOfDay = 0.0f;
};
