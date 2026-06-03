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
    // Starting inventory (slots 4..26 begin empty)
    m_inventory[0] = { ItemType::TOOL_HOE,         1  };
    m_inventory[1] = { ItemType::TOOL_WATERINGCAN, 1  };
    m_inventory[2] = { ItemType::SEED_WHEAT,       10 };
    m_inventory[3] = { ItemType::TOOL_AXE,         1  };
    m_inventory[4] = { ItemType::TOOL_SICKLE,      1  };
    m_inventory[5] = { ItemType::TOOL_PICKAXE,     1  };
}

void GameState::update(float dt, const PlayerInput& input, const Camera& camera, World& world) {
    // Time
    m_time += dt;
    m_day = static_cast<int>(m_time / DAY_DURATION);
    m_timeOfDay = std::fmod(m_time, DAY_DURATION) / DAY_DURATION;

    // Growth tick once per day
    if (m_day != m_prevDay) {
        world.growthTick(m_day);
        m_prevDay = m_day;
    }

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
        // Crafting clicks (inventory open) — edge-detected so holding doesn't repeat-craft.
        if (m_inventoryOpen && input.leftClick && !m_prevCraftClick) {
            int n = 0;
            const Recipe* table = craftingRecipes(n);
            for (int i = 0; i < n; i++) {
                if (table[i].requiresWorkbench && !m_nearWorkbench) continue; // workbench-gated
                float rx, ry, rw, rh;
                craftRowRect(i, (float)input.windowWidth, (float)input.windowHeight, rx, ry, rw, rh);
                if (input.mouseX >= rx && input.mouseX <= rx + rw &&
                    input.mouseY >= ry && input.mouseY <= ry + rh) {
                    craft(i);
                    break;
                }
            }
        }
        m_prevCraftClick = input.leftClick;

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

                        if (input.leftClick) {
                            const int tx = finalTargetTile.x;
                            const int ty = finalTargetTile.y;
                            const int tz = finalTargetTile.z;
                            const ItemType sel = m_inventory[m_selectedSlot].type;

                            // 1) World object (tree/rock) takes priority — harvest with matching tool
                            glm::vec3 objPos; ItemType objDrop; int objCount;
                            World::HarvestResult hr = world.tryHarvestObject(tx, ty, sel, objPos, objDrop, objCount);
                            if (hr == World::HarvestResult::Harvested) {
                                // objPos.z is the ground surface — lift the drop so the cube rests on top.
                                for (int i = 0; i < objCount; i++) {
                                    glm::vec3 dropPos = glm::vec3(objPos.x + 0.15f * i, objPos.y, objPos.z + 0.2f);
                                    m_drops.push_back({ dropPos, objDrop, 1 });
                                }
                            }
                            // 2) Object present but wrong tool → blocked (don't destroy ground beneath it)
                            else if (hr == World::HarvestResult::WrongTool) {
                                // no-op
                            }
                            // 3) No object — crop harvest only. Terrain is immutable
                            //    (voxel destruction retired; see DESIGN "지형 불변").
                            else if (world.getTile(tx, ty, tz) == TileType::WHEAT) {
                                // Crops are protected: only a sickle can harvest, and only when ripe.
                                TileState s = world.getTileState(tx, ty, tz);
                                if (sel == ItemType::TOOL_SICKLE && s.growthStage == 3) {
                                    world.setTile(tx, ty, tz, TileType::AIR);
                                    glm::vec3 dropPos = glm::vec3(finalTargetTile) + glm::vec3(0.0f, 0.0f, -0.25f);
                                    m_drops.push_back({ dropPos, ItemType::ITEM_WHEAT, 1 });
                                }
                            }
                        }

                        if (input.rightClick) {
                            ItemStack& slot = m_inventory[m_selectedSlot];
                            ItemType item = slot.type;
                            const int tx = finalTargetTile.x;
                            const int ty = finalTargetTile.y;
                            const int tz = finalTargetTile.z;

                            // Voxel block placement retired (DESIGN "지형 불변"); building is the
                            // crafted-object layer. Right-click now only drives farming tools.
                            if (item == ItemType::TOOL_HOE) {
                                TileType cur = world.getTile(tx, ty, tz);
                                if (cur == TileType::GRASS || cur == TileType::DIRT)
                                    world.setTile(tx, ty, tz, TileType::FARMLAND);
                            } else if (item == ItemType::SEED_WHEAT) {
                                if (slot.count > 0 &&
                                    world.getTile(tx, ty, tz) == TileType::FARMLAND &&
                                    world.getTile(tx, ty, tz + 1) == TileType::AIR) {
                                    world.setTile(tx, ty, tz + 1, TileType::WHEAT);
                                    TileState s;
                                    s.growthStage    = 0;
                                    s.lastUpdatedDay = (uint32_t)m_day;
                                    world.setTileState(tx, ty, tz + 1, s);
                                    if (--slot.count <= 0) slot = ItemStack{}; // consume seed
                                }
                            } else if (item == ItemType::TOOL_WATERINGCAN) {
                                // A planted crop occupies the tile above its soil, so the picked
                                // tile is the WHEAT — water the FARMLAND directly beneath it.
                                int fz = tz;
                                if (world.getTile(tx, ty, fz) == TileType::WHEAT) fz -= 1;
                                if (world.getTile(tx, ty, fz) == TileType::FARMLAND) {
                                    TileState s = world.getTileState(tx, ty, fz);
                                    s.watered = true;
                                    world.setTileState(tx, ty, fz, s);
                                }
                            } else {
                                // Placeable item (workbench / fence) → place it on the ground.
                                ObjectType otype;
                                if (itemToObjectType(item, otype) && slot.count > 0 &&
                                    world.placeObject(tx, ty, otype)) {
                                    if (--slot.count <= 0) slot = ItemStack{};
                                }
                            }
                        }
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
