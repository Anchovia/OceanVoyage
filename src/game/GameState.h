#pragma once
#include <glm/glm.hpp>

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
// and rudder.
struct ShipState {
    glm::vec2 position{15.0f, 15.0f};
    glm::vec2 velocity{0.0f, 0.0f};
    float heading  = -1.5707963f; // radians (-pi/2 = facing -Y)
    float yawRate  = 0.0f;
    float throttle = 0.0f;        // -1..1 forward/reverse demand
    float rudder   = 0.0f;        // -1..1 port/starboard demand
};

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

    int   day()       const { return m_day; }
    float timeOfDay() const { return m_timeOfDay; } // 0.0=midnight, 0.5=noon, 1.0=midnight
    float time()      const { return m_time; }

    // Restores the full sailing state (position/velocity/heading/yawRate/
    // throttle/rudder) from a save.
    void setShipState(const ShipState& s) { m_ship = s; }
    void setTime(float t);

private:
    // Integrates the ship's sailing physics from WASD (throttle/rudder) input.
    void updateShipPhysics(float dt, const PlayerInput& input);

    ShipState m_ship;

    float m_time      = 0.0f;
    int   m_day       = 0;
    int   m_prevDay   = -1;
    float m_timeOfDay = 0.0f;
};
