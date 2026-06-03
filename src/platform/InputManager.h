#pragma once
#include "game/GameState.h" // For PlayerInput struct
#include "platform/Window.h"

class InputManager {
public:
    InputManager(Window& window);

    // Called each frame to return current input snapshot
    PlayerInput pollInput();

private:
    Window& m_window;
};
