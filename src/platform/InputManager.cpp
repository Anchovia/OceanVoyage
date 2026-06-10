#include "platform/InputManager.h"

InputManager::InputManager(Window& window) : m_window(window) {}

PlayerInput InputManager::pollInput() {
    PlayerInput input{};
    GLFWwindow* win = m_window.handle();

    // S doubles as part of Ctrl+S (save); suppress backward movement while Ctrl is held.
    const bool ctrlHeld = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;

    // Movement keys
    input.moveForward  = glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS;
    input.moveBackward = glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS && !ctrlHeld;
    input.moveLeft     = glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS;
    input.moveRight    = glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS;

    // Scroll wheel (camera zoom)
    double scroll = m_window.consumeScrollY();
    if (scroll != 0.0)
        input.scrollDelta = (scroll > 0.0) ? -1 : 1;

    // Mouse position — scale cursor (window/screen coords) to framebuffer pixels so it
    // matches windowWidth/Height (framebuffer) and the render/swapchain space on HiDPI.
    double cursorX, cursorY;
    glfwGetCursorPos(win, &cursorX, &cursorY);
    int winW = 0, winH = 0, fbW = 0, fbH = 0;
    glfwGetWindowSize(win, &winW, &winH);
    glfwGetFramebufferSize(win, &fbW, &fbH);
    input.mouseX       = (winW > 0) ? cursorX * (double)fbW / winW : cursorX;
    input.mouseY       = (winH > 0) ? cursorY * (double)fbH / winH : cursorY;
    input.windowWidth  = fbW;
    input.windowHeight = fbH;
    input.leftClick  = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    input.rightClick = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    // Camera / system keys
    input.quit        = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    input.rotateLeft  = glfwGetKey(win, GLFW_KEY_Q)      == GLFW_PRESS;
    input.rotateRight = glfwGetKey(win, GLFW_KEY_E)      == GLFW_PRESS;
    input.saveKey     = ctrlHeld && glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS;
    input.toggleDevUi = glfwGetKey(win, GLFW_KEY_F3)     == GLFW_PRESS;

    return input;
}
