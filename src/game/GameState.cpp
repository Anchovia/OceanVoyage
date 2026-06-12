#include "game/GameState.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#include <algorithm>

namespace {
// Sailing physics tuning (first pass — expect to retune after a build).
constexpr float kMaxForwardSpeed    = 9.0f;  // units/s
constexpr float kMaxReverseSpeed    = 2.0f;  // units/s (reverse is slow and sluggish)
constexpr float kThrust             = 5.5f;  // forward accel per full throttle (units/s^2)
constexpr float kLinearDrag         = 0.35f; // velocity damping per second
constexpr float kVelocitySnap       = 0.05f; // below this speed, kill micro-drift
constexpr float kTurnPower          = 1.2f;  // yaw accel per full rudder at speed
constexpr float kYawDamping         = 1.8f;  // yaw-rate damping per second
constexpr float kYawSnap            = 0.01f; // below this yaw rate, snap to 0
constexpr float kSpeedForFullRudder = 3.0f;  // speed at which the rudder has full authority
constexpr float kThrottleRate       = 0.8f;  // throttle change per second while keyed
constexpr float kThrottleReturn     = 0.5f;  // throttle ease-back per second with no input
constexpr float kRudderRate         = 2.5f;  // rudder change per second while keyed
constexpr float kRudderReturn       = 3.0f;  // rudder centering per second with no input
constexpr float kDockMaxSpeed       = 2.0f;  // ship must be this slow (units/s) to dock
constexpr float kIslandClearance    = 6.0f;  // hull keep-out margin beyond the island waterline ellipse

// Move v toward 0 by at most `amount`.
float approachZero(float v, float amount) {
    if (v >  amount) return v - amount;
    if (v < -amount) return v + amount;
    return 0.0f;
}
}

void GameState::update(float dt, const PlayerInput& input) {
    // Time
    m_time += dt;
    m_day = static_cast<int>(m_time / DAY_DURATION);
    m_timeOfDay = std::fmod(m_time, DAY_DURATION) / DAY_DURATION;

    // Route destination (T, edge-detected): cycle none -> each port -> none.
    // Works while docked too, so the next leg can be planned in harbor.
    const bool routePressed = input.routeKey && !m_prevRouteKey;
    m_prevRouteKey = input.routeKey;
    if (routePressed && !m_marketOpen) {
        const int count = (int)m_world.ports().size();
        m_routeTargetPortId = (m_routeTargetPortId + 2) % (count + 1) - 1;
    }

    // Docking (Enter, edge-detected): anchor at the nearby port and open the
    // port menu. Undocking happens through the menu (setSail).
    const bool dockPressed = input.dockKey && !m_prevDockKey;
    m_prevDockKey = input.dockKey;
    if (m_mode == GameMode::Sailing && dockPressed && canDock()) {
        const std::vector<Port>& ports = m_world.ports();
        for (size_t i = 0; i < ports.size(); i++) {
            if (glm::length(ports[i].position - m_ship.position) <= ports[i].radius) {
                m_mode            = GameMode::Docked;
                m_dockedPortIndex = (int)i;
                m_ship.velocity   = glm::vec2(0.0f);
                m_ship.yawRate    = 0.0f;
                m_ship.throttle   = 0.0f;
                m_ship.rudder     = 0.0f;
                // Arriving at the route destination completes the route.
                if (ports[i].id == m_routeTargetPortId)
                    m_routeTargetPortId = -1;
                break;
            }
        }
    }

    // While docked the ship stays anchored: no physics integration. Buoyancy
    // still floats the hull visually (renderer-side FFT readback).
    if (m_mode == GameMode::Docked) {
        updateMarket(input);
        return;
    }

    // --- Ship sailing physics (replaces the farm camera-relative tile walk) ---
    updateShipPhysics(dt, input);
}

void GameState::updateMarket(const PlayerInput& input) {
    const bool upPressed   = input.menuUp   && !m_prevMenuUp;
    const bool downPressed = input.menuDown && !m_prevMenuDown;
    const bool buyPressed  = input.buyKey   && !m_prevBuyKey;
    const bool sellPressed = input.sellKey  && !m_prevSellKey;
    m_prevMenuUp   = input.menuUp;
    m_prevMenuDown = input.menuDown;
    m_prevBuyKey   = input.buyKey;
    m_prevSellKey  = input.sellKey;

    if (!m_marketOpen || m_dockedPortIndex < 0)
        return;
    Port& port = m_world.portAt((size_t)m_dockedPortIndex);
    if (port.market.empty())
        return;

    const int rowCount = (int)port.market.size();
    if (upPressed)   m_marketSelected = std::max(0, m_marketSelected - 1);
    if (downPressed) m_marketSelected = std::min(rowCount - 1, m_marketSelected + 1);
    m_marketSelected = std::clamp(m_marketSelected, 0, rowCount - 1);

    MarketEntry& entry = port.market[(size_t)m_marketSelected];
    if (buyPressed && entry.stock > 0 && m_money >= entry.buyPrice
        && m_cargo.used() < m_cargo.capacity) {
        m_money -= entry.buyPrice;
        entry.stock -= 1;
        cargoAdd(entry.good, 1);
    }
    if (sellPressed && cargoRemove(entry.good, 1)) {
        m_money += entry.sellPrice;
        entry.stock += 1;
    }
}

int GameState::cargoCount(CargoGoodId good) const {
    int total = 0;
    for (const CargoStack& s : m_cargo.stacks)
        if (s.good == good) total += s.count;
    return total;
}

void GameState::cargoAdd(CargoGoodId good, int count) {
    for (CargoStack& s : m_cargo.stacks) {
        if (s.good == good) {
            s.count += count;
            return;
        }
    }
    m_cargo.stacks.push_back({ good, count });
}

bool GameState::cargoRemove(CargoGoodId good, int count) {
    // cargoAdd always merges, so each good has at most one stack.
    for (auto it = m_cargo.stacks.begin(); it != m_cargo.stacks.end(); ++it) {
        if (it->good != good) continue;
        if (it->count < count) return false;
        it->count -= count;
        if (it->count == 0)
            m_cargo.stacks.erase(it);
        return true;
    }
    return false;
}

bool GameState::canDock() const {
    if (m_mode != GameMode::Sailing) return false;
    if (glm::length(m_ship.velocity) > kDockMaxSpeed) return false;
    for (const Port& p : m_world.ports())
        if (glm::length(p.position - m_ship.position) <= p.radius) return true;
    return false;
}

void GameState::updateShipPhysics(float dt, const PlayerInput& input) {
    // --- Map WASD to throttle / rudder demand ---
    const float throttleInput = (input.moveForward  ? 1.0f : 0.0f)
                              - (input.moveBackward ? 1.0f : 0.0f);
    if (throttleInput != 0.0f)
        m_ship.throttle = std::clamp(m_ship.throttle + throttleInput * kThrottleRate * dt, -1.0f, 1.0f);
    else
        m_ship.throttle = approachZero(m_ship.throttle, kThrottleReturn * dt);

    // D = starboard (right), A = port (left).
    const float rudderInput = (input.moveRight ? 1.0f : 0.0f)
                            - (input.moveLeft  ? 1.0f : 0.0f);
    if (rudderInput != 0.0f)
        m_ship.rudder = std::clamp(m_ship.rudder + rudderInput * kRudderRate * dt, -1.0f, 1.0f);
    else
        m_ship.rudder = approachZero(m_ship.rudder, kRudderReturn * dt);

    // --- Integrate hull motion ---
    const glm::vec2 forward{ std::cos(m_ship.heading), std::sin(m_ship.heading) };
    const float devMul = std::clamp(input.moveSpeedMultiplier, 0.1f, 12.0f); // dev fast-move; 1.0 in normal play

    // Wind assist (Phase 5 first pass): a gentle scalar from the relative
    // wind — tailwind helps, headwind hinders, capped at roughly ±15%. The
    // real sail model (efficiency curve, no-go zone, tacking) is Phase 7.
    const Wind wind = m_world.windAt(m_time);
    const float windAlong = glm::dot(wind.direction, forward);
    const float windFactor = std::clamp(
        1.0f + windAlong * (wind.speed / 10.0f) * 0.15f, 0.82f, 1.18f);

    // Thrust along the bow, then linear water drag.
    m_ship.velocity += forward * (m_ship.throttle * kThrust * windFactor * devMul * dt);
    m_ship.velocity -= m_ship.velocity * (kLinearDrag * dt);

    // Clamp speed (asymmetric: reverse is much slower than forward), and kill
    // tiny residual drift so a released ship settles instead of creeping.
    // Tailwind also lifts the forward top speed a little.
    float speed = glm::length(m_ship.velocity);
    if (speed > kVelocitySnap) {
        const float along = glm::dot(m_ship.velocity, forward);
        const float limit = ((along >= 0.0f) ? kMaxForwardSpeed * windFactor
                                             : kMaxReverseSpeed) * devMul;
        if (speed > limit) m_ship.velocity *= (limit / speed);
    } else {
        m_ship.velocity = glm::vec2{ 0.0f };
        speed = 0.0f;
    }

    // Rudder authority grows with speed → no spinning in place when stopped.
    // Positive rudder (starboard) turns the bow clockwise (heading decreases).
    const float speedFactor = std::clamp(speed / kSpeedForFullRudder, 0.0f, 1.0f);
    m_ship.yawRate += (-m_ship.rudder) * kTurnPower * speedFactor * dt;
    m_ship.yawRate -= m_ship.yawRate * (kYawDamping * dt);
    if (std::abs(m_ship.yawRate) < kYawSnap) m_ship.yawRate = 0.0f;

    m_ship.heading  += m_ship.yawRate * dt;
    m_ship.position += m_ship.velocity * dt;

    resolveIslandCollision();
}

// Ellipse-distance collision (ROADMAP Phase 5: no tile grid): if the hull is
// inside an island's inflated waterline ellipse, push it back to the boundary
// and remove the inward velocity component so the ship slides along the shore
// instead of sticking.
void GameState::resolveIslandCollision() {
    for (const Island& isl : m_world.islands()) {
        const float c = std::cos(isl.rotation);
        const float s = std::sin(isl.rotation);
        const glm::vec2 rel = m_ship.position - isl.center;
        // World -> island-local frame (inverse rotation).
        const glm::vec2 local{ rel.x * c + rel.y * s, -rel.x * s + rel.y * c };
        const float rx = isl.radiusX + kIslandClearance;
        const float ry = isl.radiusY + kIslandClearance;
        const glm::vec2 e{ local.x / rx, local.y / ry };
        const float d2 = glm::dot(e, e);
        if (d2 >= 1.0f || d2 < 1.0e-6f) continue;

        // Exact push-out along the center ray (the ellipse scales linearly),
        // with the outward normal from the ellipse gradient at that point.
        const glm::vec2 boundaryLocal = local / std::sqrt(d2);
        const glm::vec2 nLocal = glm::normalize(
            glm::vec2{ boundaryLocal.x / (rx * rx), boundaryLocal.y / (ry * ry) });
        const glm::vec2 nWorld{ nLocal.x * c - nLocal.y * s,
                                nLocal.x * s + nLocal.y * c };
        m_ship.position = isl.center + glm::vec2{ boundaryLocal.x * c - boundaryLocal.y * s,
                                                  boundaryLocal.x * s + boundaryLocal.y * c };
        const float vn = glm::dot(m_ship.velocity, nWorld);
        if (vn < 0.0f) m_ship.velocity -= nWorld * vn;
    }
}

void GameState::setTime(float t) {
    m_time      = t;
    m_day       = static_cast<int>(m_time / DAY_DURATION);
    m_prevDay   = m_day;
    m_timeOfDay = std::fmod(m_time, DAY_DURATION) / DAY_DURATION;
}
