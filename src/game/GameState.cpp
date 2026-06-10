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

// Move v toward 0 by at most `amount`.
float approachZero(float v, float amount) {
    if (v >  amount) return v - amount;
    if (v < -amount) return v + amount;
    return 0.0f;
}
}

GameState::GameState() {
    // Inventory starts empty for OceanVoyage. The farm starting tools (hoe, watering
    // can, seeds, axe, sickle, pickaxe) were removed in the farm-gameplay transition;
    // a cargo/ship inventory will replace them later. m_inventory default-initializes
    // every slot to NONE/0, so no explicit clearing is needed here.
}

void GameState::update(float dt, const PlayerInput& input) {
    // Time
    m_time += dt;
    m_day = static_cast<int>(m_time / DAY_DURATION);
    m_timeOfDay = std::fmod(m_time, DAY_DURATION) / DAY_DURATION;

    // Crop growth tick removed for OceanVoyage transition (farm gameplay disabled).
    // World::growthTick is kept as reference until the ocean systems replace it.

    // Inventory toggle (I key — edge-detect to avoid repeated triggers)
    if (input.toggleInventory && !m_prevToggleInv)
        m_inventoryOpen = !m_inventoryOpen;
    m_prevToggleInv = input.toggleInventory;

    // Hotbar selection
    if (input.selectSlot >= 0 && input.selectSlot < HOTBAR_SLOTS)
        m_selectedSlot = input.selectSlot;
    if (input.scrollDelta != 0) {
        m_selectedSlot = (m_selectedSlot + input.scrollDelta) % HOTBAR_SLOTS;
        if (m_selectedSlot < 0) m_selectedSlot += HOTBAR_SLOTS;
    }

    // --- Ship sailing physics (replaces the farm camera-relative tile walk) ---
    updateShipPhysics(dt, input);

    // Mirror the ship into the legacy Player so existing consumers — camera
    // target, chunk streaming, wake/buoyancy/shadow inputs, and save — follow
    // the ship with no other changes. Player and the FrameRenderData "player*"
    // names are a temporary shim to be removed in a later phase.
    constexpr float kShipDeckZ = 1.0f; // height only feeds shadow/camera; buoyancy recomputes the visual height
    m_player.setPosition(glm::vec3(m_ship.position.x, m_ship.position.y, kShipDeckZ));
    m_player.setFacingDirection(glm::vec2(std::cos(m_ship.heading), std::sin(m_ship.heading)));

    // Farm interaction (drop pickup, nearby-workbench detection, mouse-ray tile
    // picking / targetTile) removed for OceanVoyage: the sailing loop no longer
    // touches World or Camera. m_targetTile stays empty (tile selector inert) and
    // m_drops / m_nearWorkbench keep their defaults. Their members, getters,
    // FrameRenderData fields, and renderer plumbing are removed in a later step.
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

    // Thrust along the bow, then linear water drag.
    m_ship.velocity += forward * (m_ship.throttle * kThrust * devMul * dt);
    m_ship.velocity -= m_ship.velocity * (kLinearDrag * dt);

    // Clamp speed (asymmetric: reverse is much slower than forward), and kill
    // tiny residual drift so a released ship settles instead of creeping.
    float speed = glm::length(m_ship.velocity);
    if (speed > kVelocitySnap) {
        const float along = glm::dot(m_ship.velocity, forward);
        const float limit = ((along >= 0.0f) ? kMaxForwardSpeed : kMaxReverseSpeed) * devMul;
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
}

bool GameState::addItem(ItemType type, int count) {
    // Fill an existing stack of the same type first.
    for (ItemStack& slot : m_inventory) {
        if (slot.type == type && slot.count > 0) {
            slot.count += count;
            return true;
        }
    }
    // Otherwise use the first empty slot.
    for (ItemStack& slot : m_inventory) {
        if (slot.type == ItemType::NONE || slot.count <= 0) {
            slot = { type, count };
            return true;
        }
    }
    return false; // inventory full
}

int GameState::countItem(ItemType type) const {
    int total = 0;
    for (const ItemStack& s : m_inventory)
        if (s.type == type) total += s.count;
    return total;
}

bool GameState::removeItem(ItemType type, int count) {
    if (countItem(type) < count) return false;
    for (ItemStack& s : m_inventory) {
        if (s.type != type) continue;
        int take = std::min(s.count, count);
        s.count -= take;
        count   -= take;
        if (s.count <= 0) s = ItemStack{};
        if (count == 0) break;
    }
    return true;
}

bool GameState::craft(int recipeIndex) {
    int n = 0;
    const Recipe* table = craftingRecipes(n);
    if (recipeIndex < 0 || recipeIndex >= n) return false;
    const Recipe& r = table[recipeIndex];

    // Need every input in stock
    for (const RecipeInput& in : r.inputs) {
        if (in.type == ItemType::NONE) continue;
        if (countItem(in.type) < in.count) return false;
    }
    // Consume inputs
    for (const RecipeInput& in : r.inputs) {
        if (in.type == ItemType::NONE) continue;
        removeItem(in.type, in.count);
    }
    // Produce result; roll back the inputs if there's no room
    if (!addItem(r.result, r.resultCount)) {
        for (const RecipeInput& in : r.inputs)
            if (in.type != ItemType::NONE) addItem(in.type, in.count);
        return false;
    }
    return true;
}

void GameState::setPlayerPosition(const glm::vec3& pos) {
    m_player.setPosition(pos);
    // Keep the ship (source of truth) in sync so a loaded/teleported position is
    // not immediately overwritten by the mirror in update(). Heading is not yet
    // persisted (arrives with VoyageSave); reset motion on a hard set.
    m_ship.position = glm::vec2(pos.x, pos.y);
    m_ship.velocity = glm::vec2(0.0f);
    m_ship.yawRate  = 0.0f;
}

void GameState::setShipState(const ShipState& s) {
    m_ship = s;
    // Re-sync the legacy Player mirror immediately (same shim as update()) so
    // camera snap and chunk streaming see the restored position this frame.
    constexpr float kShipDeckZ = 1.0f;
    m_player.setPosition(glm::vec3(m_ship.position.x, m_ship.position.y, kShipDeckZ));
    m_player.setFacingDirection(glm::vec2(std::cos(m_ship.heading), std::sin(m_ship.heading)));
}

void GameState::setTime(float t) {
    m_time      = t;
    m_day       = static_cast<int>(m_time / DAY_DURATION);
    m_prevDay   = m_day; // suppress immediate growthTick on load
    m_timeOfDay = std::fmod(m_time, DAY_DURATION) / DAY_DURATION;
}

void GameState::setInventory(const std::array<ItemStack, INV_SLOTS>& inv) {
    m_inventory = inv;
}

void GameState::setDrops(const std::vector<DroppedItem>& drops) {
    m_drops = drops;
}
