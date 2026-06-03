#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void close() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
    void pollEvents();

    GLFWwindow* handle() const { return m_window; }
    int width()  const { return m_width; }
    int height() const { return m_height; }

    bool wasResized() const { return m_resized; }
    void resetResized()     { m_resized = false; }

    // Returns accumulated scroll since last call, then resets to zero
    double consumeScrollY() { double s = m_scrollY; m_scrollY = 0.0; return s; }

private:
    static void resizeCallback(GLFWwindow* window, int w, int h);
    static void scrollCallback(GLFWwindow* window, double xoff, double yoff);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);

    GLFWwindow* m_window = nullptr;
    int    m_width, m_height;
    bool   m_resized = false;
    double m_scrollY = 0.0;
};
