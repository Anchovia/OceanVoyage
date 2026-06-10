#include "game/GameState.h"
#include "game/VoyageSave.h"
#include "platform/Window.h"
#include "platform/InputManager.h"
#include "renderer/VulkanContext.h"
#include "world/World.h"
#include "world/Chunk.h"
#include <iostream>
#include <cmath>
#include "game/Camera.h"

static constexpr int LOAD_RADIUS   = 3;
static constexpr int UNLOAD_RADIUS = 4;
static constexpr int CHUNK_STREAM_LOADS_PER_FRAME = 2;

enum class AppMode {
    MainMenu,
    Settings,
    Loading,
    Gameplay,
    Paused,
};

struct AppFlow {
    AppMode mode = AppMode::MainMenu;
    bool prevEsc = false;
    bool prevMenuClick = false;
    bool prevPauseClick = false;
    bool prevCtrlS = false;
    AppMode settingsReturnMode = AppMode::MainMenu;
#ifdef PASTEL_DEV_BUILD
    bool prevDevUiToggle = false;
#endif

    bool gameplayActive() const {
        return mode == AppMode::Gameplay;
    }

    bool paused() const {
        return mode == AppMode::Paused;
    }

    bool mainMenu() const {
        return mode == AppMode::MainMenu;
    }

    bool settings() const {
        return mode == AppMode::Settings;
    }

    bool loading() const {
        return mode == AppMode::Loading;
    }

    int consumeMainMenuClick(const PlayerInput& input) {
        int action = 0; // 0=none, 1=start, 2=settings
        if (mode == AppMode::MainMenu && input.leftClick && !prevMenuClick) {
            float x, y, w, h;
            mainMenuRowRect(0, (float)input.windowWidth, (float)input.windowHeight, x, y, w, h);
            if (input.mouseX >= x && input.mouseX <= x + w &&
                input.mouseY >= y && input.mouseY <= y + h) {
                action = 1;
            }

            mainMenuRowRect(1, (float)input.windowWidth, (float)input.windowHeight, x, y, w, h);
            if (input.mouseX >= x && input.mouseX <= x + w &&
                input.mouseY >= y && input.mouseY <= y + h) {
                action = 2;
            }
        }
        prevMenuClick = input.leftClick;
        return action;
    }

    int consumePauseClick(const PlayerInput& input) {
        int action = 0; // 0=none, 1=resume, 2=settings, 3=quit
        if (mode == AppMode::Paused && input.leftClick && !prevPauseClick) {
            float x, y, w, h;
            for (int i = 0; i < 3; ++i) {
                pauseMenuRowRect(i, (float)input.windowWidth, (float)input.windowHeight, x, y, w, h);
                if (input.mouseX >= x && input.mouseX <= x + w &&
                    input.mouseY >= y && input.mouseY <= y + h) {
                    action = i + 1;
                }
            }
        }
        prevPauseClick = input.leftClick;
        return action;
    }

    void enterSettings() {
        if (mode == AppMode::MainMenu || mode == AppMode::Paused) {
            settingsReturnMode = mode;
            mode = AppMode::Settings;
        }
    }

    void enterLoading() {
        if (mode == AppMode::MainMenu)
            mode = AppMode::Loading;
    }

    void enterGameplay() {
        if (mode == AppMode::Loading)
            mode = AppMode::Gameplay;
    }

    void leaveSettings() {
        if (mode == AppMode::Settings)
            mode = settingsReturnMode;
    }

    void resumeGameplay() {
        if (mode == AppMode::Paused)
            mode = AppMode::Gameplay;
    }

    void returnToTitle() {
        if (mode == AppMode::Paused)
            mode = AppMode::MainMenu;
    }

    void updateEscape(bool escPressed) {
        if (escPressed && !prevEsc) {
            if (mode == AppMode::Gameplay)
                mode = AppMode::Paused;
            else if (mode == AppMode::Paused)
                mode = AppMode::Gameplay;
            else if (mode == AppMode::Settings)
                leaveSettings();
        }
        prevEsc = escPressed;
    }

    bool consumeInventoryEscape(bool escPressed, bool inventoryOpen) {
        const bool pressed = mode == AppMode::Gameplay && inventoryOpen && escPressed && !prevEsc;
        if (pressed)
            prevEsc = escPressed;
        return pressed;
    }

    bool consumeSavePress(bool savePressed) {
        const bool pressed = savePressed && !prevCtrlS;
        prevCtrlS = savePressed;
        return pressed;
    }

#ifdef PASTEL_DEV_BUILD
    bool consumeDevUiToggle(bool togglePressed) {
        const bool pressed = togglePressed && !prevDevUiToggle;
        prevDevUiToggle = togglePressed;
        return pressed;
    }
#endif
};

struct AppSettings {
    bool vsync = true;
    int aaMode = 2; // 0=off, 1=FXAA, 2=SMAA
    bool prevClick = false;

    void syncClickState(const PlayerInput& input) {
        prevClick = input.leftClick;
    }

    bool update(const PlayerInput& input, AppMode mode) {
        bool backClicked = false;
        if (mode == AppMode::Settings && input.leftClick && !prevClick) {
            float x, y, w, h;
            settingsRowRect(0, (float)input.windowWidth, (float)input.windowHeight, x, y, w, h);
            if (input.mouseX >= x && input.mouseX <= x + w &&
                input.mouseY >= y && input.mouseY <= y + h) {
                vsync = !vsync;
            }

            settingsRowRect(1, (float)input.windowWidth, (float)input.windowHeight, x, y, w, h);
            if (input.mouseX >= x && input.mouseX <= x + w &&
                input.mouseY >= y && input.mouseY <= y + h) {
                aaMode = (aaMode + 1) % 3;
            }

            settingsRowRect(2, (float)input.windowWidth, (float)input.windowHeight, x, y, w, h);
            if (input.mouseX >= x && input.mouseX <= x + w &&
                input.mouseY >= y && input.mouseY <= y + h) {
                backClicked = true;
            }
        }
        prevClick = input.leftClick;
        return backClicked;
    }
};

static void clearGameplayInput(PlayerInput& input) {
    input.moveForward     = false;
    input.moveBackward    = false;
    input.moveLeft        = false;
    input.moveRight       = false;
    input.leftClick       = false;
    input.rightClick      = false;
    input.toggleInventory = false;
    input.rotateLeft      = false;
    input.rotateRight     = false;
    input.selectSlot      = -1;
    input.scrollDelta     = 0;
}

static void applyAppModeInputPolicy(PlayerInput& input, AppMode mode) {
    if (mode != AppMode::Gameplay) {
        clearGameplayInput(input);
        input.saveKey = false;
    }
}

#ifdef PASTEL_DEV_BUILD
static void applyDevUiInputCapture(PlayerInput& input, const VulkanContext& ctx) {
    if (ctx.devWantsMouse()) {
        input.leftClick   = false;
        input.rightClick  = false;
        input.scrollDelta = 0;
    }
    if (ctx.devWantsKeyboard()) {
        clearGameplayInput(input);
        input.saveKey         = false;
        input.quit            = false;
    }
}
#endif

int main() {
    try {
        Window        window(1280, 720, "OceanVoyage");
        World         world;
        GameState     gameState;
        VulkanContext ctx(window);
        InputManager  inputManager(window);
        Camera camera(45.0f, 1280.0f / 720.0f, 0.1f, 3000.0f);

        AppFlow app;
        AppSettings settings;
        bool worldSessionStarted = false;
        bool pendingWorldStart = false;
        glm::ivec2 lastPlayerChunk{0, 0};

        auto startWorldSession = [&]() {
            // VoyageSave ("OVYG") restores the sailing state. A legacy farm
            // "PFRM" save.dat fails the magic check and starts a new game.
            VoyageSave::Data saved;
            if (VoyageSave::load("save.dat", saved)) {
                gameState.setTime(saved.gameTime);
                gameState.setShipState(saved.ship);
            }

            lastPlayerChunk = World::chunkCoord(
                (int)gameState.player().position().x,
                (int)gameState.player().position().y
            );
            world.loadChunksAround(lastPlayerChunk.x, lastPlayerChunk.y, LOAD_RADIUS);
            worldSessionStarted = true;
        };

        // Tear down the active world session (quit to title). Drops all chunks so the
        // renderer frees their buffers, and resets gameplay state for a fresh re-start.
        auto endWorldSession = [&]() {
            world.reset();
            gameState = GameState{};
            worldSessionStarted = false;
            pendingWorldStart   = false;
        };

        float  orbitAngle = 45.0f;
        double lastTime   = glfwGetTime();

        while (!window.shouldClose()) {
            window.pollEvents();
#ifdef PASTEL_DEV_BUILD
            ctx.beginDevFrame();
#endif

            double now = glfwGetTime();
            float  dt  = static_cast<float>(now - lastTime);
            lastTime = now;
            // Clamp dt: after a stall/resize/debugger pause a huge frame would jump
            // time, growth, and movement (and tunnel through collision) in one step.
            if (dt > 0.1f) dt = 0.1f;

            PlayerInput input = inputManager.pollInput();
#ifdef PASTEL_DEV_BUILD
            if (app.consumeDevUiToggle(input.toggleDevUi))
                ctx.toggleDevUi();
            applyDevUiInputCapture(input, ctx);
            input.moveSpeedMultiplier = ctx.devMoveSpeedMultiplier();
#endif

            if (app.consumeInventoryEscape(input.quit, gameState.inventoryOpen())) {
                gameState.closeInventory();
                input.quit = false;
            } else {
                app.updateEscape(input.quit);
            }
            const bool wasSettings = app.settings();
            const int menuClickAction = app.consumeMainMenuClick(input);
            const int pauseClickAction = app.consumePauseClick(input);
            if (menuClickAction == 2) {
                app.enterSettings();
                settings.syncClickState(input);
            }
            if (pauseClickAction == 1) {
                app.resumeGameplay();
                clearGameplayInput(input);
            }
            else if (pauseClickAction == 2) {
                app.enterSettings();
                settings.syncClickState(input);
            }
            else if (pauseClickAction == 3) {
                endWorldSession();
                app.returnToTitle();
            }
            if (wasSettings && settings.update(input, app.mode))
                app.leaveSettings();
            if (menuClickAction == 1) {
                app.enterLoading();
                pendingWorldStart = true;
                clearGameplayInput(input);
            }

            applyAppModeInputPolicy(input, app.mode);

            const float rotSpeed = 90.0f * dt;
            if (input.rotateLeft)  orbitAngle -= rotSpeed;
            if (input.rotateRight) orbitAngle += rotSpeed;

            // Scroll wheel drives chase-camera zoom (scroll up = zoom in).
            if (input.scrollDelta != 0)
                camera.zoom((float)input.scrollDelta * 2.5f);

            // Ctrl+S save (edge-detect)
            if (worldSessionStarted && app.consumeSavePress(input.saveKey))
                VoyageSave::save("save.dat", VoyageSave::Data{ gameState.time(), gameState.ship() });

            if (input.windowWidth > 0 && input.windowHeight > 0)
                camera.setAspectRatio((float)input.windowWidth / input.windowHeight);

            const glm::vec3 playerPositionBeforeUpdate = gameState.player().position();
            camera.update(playerPositionBeforeUpdate, orbitAngle, dt);
            if (app.gameplayActive())
                gameState.update(dt, input);
            const glm::vec3 playerPosition = gameState.player().position();

            // Stream chunks around the player. Generation is capped per frame so a
            // boundary crossing does not create every new edge chunk in one update.
            if (worldSessionStarted) {
                glm::ivec2 playerChunk = World::chunkCoord(
                    (int)playerPosition.x,
                    (int)playerPosition.y
                );
                if (playerChunk != lastPlayerChunk) {
                    world.unloadChunksOutside(playerChunk.x, playerChunk.y, UNLOAD_RADIUS);
                    lastPlayerChunk = playerChunk;
                }
                world.loadChunksAroundBudgeted(
                    playerChunk.x, playerChunk.y, LOAD_RADIUS,
                    CHUNK_STREAM_LOADS_PER_FRAME);
            }

            // Feed the renderer from the ship state directly (position/velocity/heading),
            // not the legacy Player mirror. Camera/chunk streaming/save still read Player
            // for now; those move off the mirror in later Phase 2 steps.
            const ShipState& ship = gameState.ship();
            const glm::vec3 shipPosition{ ship.position.x, ship.position.y, playerPosition.z };
            const glm::vec3 shipVelocity{ ship.velocity.x, ship.velocity.y, 0.0f };

            ctx.drawFrame(FrameRenderData{
                camera, shipPosition, shipVelocity, ship.heading, ship.throttle, ship.rudder,
                gameState.timeOfDay(), gameState.time(),
                app.mainMenu(), app.settings(), app.loading(), app.paused(), settings.vsync, settings.aaMode
            });

            if (pendingWorldStart && app.loading()) {
                startWorldSession();
                camera.snapToTarget(gameState.player().position(), orbitAngle);
                app.enterGameplay();
                pendingWorldStart = false;
            }
        }
        ctx.waitIdle();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
