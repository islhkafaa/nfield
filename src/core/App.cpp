#include "core/App.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

App::App(int width, int height, std::string_view title) {
  initWindow(width, height, title);
  initVulkan();
}

App::~App() { cleanup(); }

void App::initWindow(int width, int height, std::string_view title) {
  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  m_window = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }

#ifdef _WIN32
  HWND hwnd = glfwGetWin32Window(m_window);
  if (hwnd) {
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode,
                          sizeof(useDarkMode));
  }
#endif

  glfwSetWindowUserPointer(m_window, this);
  glfwSetCursorPosCallback(m_window, cursorPositionCallback);
  glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
  glfwSetScrollCallback(m_window, scrollCallback);
}

void App::initVulkan() {
  m_vulkanContext.init(m_window);
  m_vulkanContext.initParticles(8192);

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(m_window, &width, &height);
  if (height > 0) {
    m_camera.setAspect(static_cast<float>(width) / height);
  }
}

void App::cleanup() {
  m_vulkanContext.cleanup();

  if (m_window) {
    glfwDestroyWindow(m_window);
    m_window = nullptr;
  }
  glfwTerminate();
}

void App::cursorPositionCallback(GLFWwindow *window, double xpos, double ypos) {
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    double dx = xpos - app->m_lastMouseX;
    double dy = ypos - app->m_lastMouseY;

    if (app->m_leftMouseDown) {
      app->m_camera.rotate(static_cast<float>(-dx * 0.005),
                           static_cast<float>(dy * 0.005));
    } else if (app->m_rightMouseDown) {
      app->m_camera.pan(static_cast<float>(-dx * 0.05),
                        static_cast<float>(dy * 0.05));
    }

    app->m_lastMouseX = xpos;
    app->m_lastMouseY = ypos;
  }
}

void App::mouseButtonCallback(GLFWwindow *window, int button, int action,
                              int mods) {
  (void)mods;
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      app->m_leftMouseDown = (action == GLFW_PRESS);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
      app->m_rightMouseDown = (action == GLFW_PRESS);
    }
    glfwGetCursorPos(window, &app->m_lastMouseX, &app->m_lastMouseY);
  }
}

void App::scrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
  (void)xoffset;
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->m_camera.zoom(static_cast<float>(yoffset * 2.0));
  }
}

void App::run() {
  float lastTime = static_cast<float>(glfwGetTime());

  while (!glfwWindowShouldClose(m_window)) {
    glfwPollEvents();

    float currentTime = static_cast<float>(glfwGetTime());
    float dt = currentTime - lastTime;
    lastTime = currentTime;

    float G = 1.0f;
    float softeningSqr = 0.01f;

    if (dt > 0.1f) {
      dt = 0.1f;
    }

    m_vulkanContext.drawFrame(m_camera.getViewProjectionMatrix(), dt, G,
                              softeningSqr);
  }

  if (m_vulkanContext.getDevice() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_vulkanContext.getDevice());
  }
}
