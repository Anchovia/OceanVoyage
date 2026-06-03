#include "Window.h"
#include <stdexcept>

#ifdef PASTEL_DEV_BUILD
#include <imgui.h>
#include <imgui_impl_glfw.h>
#endif

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) throw std::runtime_error("Failed to create window");
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, resizeCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
#ifdef PASTEL_DEV_BUILD
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetCharCallback(m_window, charCallback);
#endif
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }
void Window::pollEvents()        { glfwPollEvents(); }

void Window::resizeCallback(GLFWwindow* window, int w, int h) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->m_width   = w;
    self->m_height  = h;
    self->m_resized = true;
}

void Window::scrollCallback(GLFWwindow* window, double xoff, double yoff) {
#ifdef PASTEL_DEV_BUILD
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_ScrollCallback(window, xoff, yoff);
#endif
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->m_scrollY += yoff;
}

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
#ifdef PASTEL_DEV_BUILD
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
#endif
}

void Window::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
#ifdef PASTEL_DEV_BUILD
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
#endif
}

void Window::charCallback(GLFWwindow* window, unsigned int codepoint) {
#ifdef PASTEL_DEV_BUILD
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_CharCallback(window, codepoint);
#endif
}
