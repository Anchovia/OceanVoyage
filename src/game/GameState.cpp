#include "game/GameState.h"
#include "world/World.h"
#include "game/Camera.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#include <algorithm>

namespace {
bool canOccupy(const World& world, const glm::vec3& position) {
    const glm::ivec3 tile = world.worldToTile(position);
    TileType body = world.getTile(tile.x, tile.y, tile.z + 1);
    bool bodyPassable = (body == TileType::AIR || body == TileType::WHEAT);
    return world.isWalkable(tile.x, tile.y, tile.z) && bodyPassable
           && !world.isCollidableAt(tile.x, tile.y);
}
}

GameState::GameState() {
    // Inventory starts empty for OceanVoyage. The farm starting tools (hoe, watering
    // can, seeds, axe, sickle, pickaxe) were removed in the farm-gameplay transition;
    // a cargo/ship inventory will replace them later. m_inventory default-initializes
    // every slot to NONE/0, so no explicit clearing is needed here.
}

void GameState::update(float dt, const PlayerInput& input, const Camera& camera, World& world) {
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

    const glm::vec3& camPos = camera.position();
    const glm::vec3& playerPos = m_player.position();

    glm::vec2 forward = glm::normalize(glm::vec2(playerPos.x - camPos.x, playerPos.y - camPos.y));
    glm::vec2 right = glm::vec2(forward.y, -forward.x);

    glm::vec2 move{ 0.0f };
    if (input.moveForward)  move += forward;
    if (input.moveBackward) move -= forward;
    if (input.moveLeft)     move -= right;
    if (input.moveRight)    move += right;

    if (glm::length(move) > 0.0f) {
        const glm::vec2 direction = glm::normalize(move);
        const glm::vec2 delta = direction * m_player.moveSpeed() * dt;
        m_player.setFacingDirection(direction);

        glm::vec3 next = m_player.position();
        next.x += delta.x;
        if (canOccupy(world, next)) {
            m_player.moveBy({ delta.x, 0.0f });
        }

        next = m_player.position();
        next.y += delta.y;
        if (canOccupy(world, next)) {
            m_player.moveBy({ 0.0f, delta.y });
        }
    }

    // Pick up nearby dropped items (horizontal distance, so vertical offset never blocks it)
    {
        constexpr float kPickupRadius = 0.9f;
        const glm::vec3 p = m_player.position();
        for (auto it = m_drops.begin(); it != m_drops.end();) {
            const float dx = it->pos.x - p.x;
            const float dy = it->pos.y - p.y;
            if (dx * dx + dy * dy <= kPickupRadius * kPickupRadius && addItem(it->type, it->count))
                it = m_drops.erase(it);
            else
                ++it;
        }
    }

    // Advanced recipes unlock only near a placed workbench.
    {
        glm::ivec3 pTile = world.worldToTile(m_player.position());
        m_nearWorkbench = world.isObjectTypeNear(pTile.x, pTile.y, ObjectType::WORKBENCH, 2);
    }

    if (input.windowWidth > 0 && input.windowHeight > 0) {
        // Crafting removed for OceanVoyage transition (farm recipes disabled).

        // World interaction — suppressed while inventory is open
        if (!m_inventoryOpen) {
            float ndcX = (2.0f * (float)input.mouseX) / input.windowWidth - 1.0f;
            float ndcY = (2.0f * (float)input.mouseY) / input.windowHeight - 1.0f;

            glm::mat4 invViewProj = glm::inverse(camera.viewProj());

            glm::vec4 target = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            glm::vec3 rayDir = glm::normalize(glm::vec3(target / target.w) - camPos);

            if (std::abs(rayDir.z) > 1e-5f) {
                float t = -camPos.z / rayDir.z;
                if (t > 0.0f) {
                    glm::vec3 hitPoint = camPos + rayDir * t;

                    glm::ivec3 pickedTile = world.worldToTile(hitPoint);

                    // Find topmost non-AIR tile at this XY
                    for (int z = CHUNK_DEPTH - 1; z >= 0; z--) {
                        if (world.getTile(pickedTile.x, pickedTile.y, z) != TileType::AIR) {
                            pickedTile.z = z;
                            break;
                        }
                    }

                    glm::ivec3 playerTile = world.worldToTile(m_player.position());

                    glm::ivec3 delta = pickedTile - playerTile;
                    delta.x = std::clamp(delta.x, -1, 1);
                    delta.y = std::clamp(delta.y, -1, 1);
                    delta.z = std::clamp(delta.z, -1, 1);

                    glm::ivec3 finalTargetTile = playerTile + delta;

                    if (world.inBounds(finalTargetTile.x, finalTargetTile.y, finalTargetTile.z)) {
                        m_targetTile = finalTargetTile;
                        // Farm tile interaction (object harvest / crop harvest / hoe / plant /
                        // water / object placement) removed for OceanVoyage transition. The
                        // World methods (tryHarvestObject / placeObject / setTileState) are kept
                        // as reference until the ocean interaction systems replace them.
                    }
                    else {
                        m_targetTile = std::nullopt;
                    }
                }
            }
        }
    }
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
