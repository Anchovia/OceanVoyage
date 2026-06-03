#pragma once
#include "game/Player.h"
#include "renderer/Types.h"

#include <optional>
#include <array>
#include <vector>

class World;
class Camera;

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
    bool toggleInventory = false;
    bool quit            = false;  // ESC (app-level pause toggle)
    bool rotateLeft      = false;  // Q
    bool rotateRight     = false;  // E
    bool saveKey         = false;  // Ctrl+S (raw; main edge-detects)
    bool toggleDevUi     = false;  // F3 (dev builds; main edge-detects)
    int  selectSlot  = -1;  // 0..HOTBAR_SLOTS-1 if a number key was pressed, else -1
    int  scrollDelta = 0;   // slots to move from scroll wheel
    int windowWidth = 1280;
    int windowHeight = 720;
};

class GameState {
public:
    GameState();

    void update(float dt, const PlayerInput& input, const Camera& camera, World& world);

    const Player& player() const { return m_player; }
    const std::optional<glm::ivec3>& targetTile() const { return m_targetTile; }

    int selectedSlot() const { return m_selectedSlot; }
    const std::array<ItemStack, INV_SLOTS>& inventory() const { return m_inventory; }

    bool inventoryOpen() const { return m_inventoryOpen; }
    void closeInventory() { m_inventoryOpen = false; }
    bool nearWorkbench() const { return m_nearWorkbench; }

    const std::vector<DroppedItem>& drops() const { return m_drops; }

    int   day()       const { return m_day; }
    float timeOfDay() const { return m_timeOfDay; } // 0.0=midnight, 0.5=noon, 1.0=midnight
    float time()      const { return m_time; }

    void setPlayerPosition(const glm::vec3& pos);
    void setTime(float t);
    void setInventory(const std::array<ItemStack, INV_SLOTS>& inv);
    void setDrops(const std::vector<DroppedItem>& drops);

private:
    // Adds count items of the given type to the inventory: fills an existing
    // matching stack first, otherwise the first empty slot. Returns false if
    // there is no room (item not added).
    bool addItem(ItemType type, int count);

    int  countItem(ItemType type) const;          // total across all slots
    bool removeItem(ItemType type, int count);     // remove across stacks; false if not enough
    bool craft(int recipeIndex);                   // consume inputs, add result

    Player m_player;
    std::optional<glm::ivec3> m_targetTile;

    int m_selectedSlot = 0;
    std::array<ItemStack, INV_SLOTS> m_inventory;

    bool m_inventoryOpen   = false;
    bool m_prevToggleInv   = false; // edge-detect for I key
    bool m_prevCraftClick  = false; // edge-detect for crafting-row clicks
    bool m_nearWorkbench   = false; // player is adjacent to a placed workbench

    std::vector<DroppedItem> m_drops; // items lying in the world awaiting pickup

    float m_time      = 0.0f;
    int   m_day       = 0;
    int   m_prevDay   = -1;
    float m_timeOfDay = 0.0f;
};
