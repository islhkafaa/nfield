#pragma once

#include "core/Camera.hpp"
#include "core/SimState.hpp"
#include "vulkan/VulkanContext.hpp"
#include <atomic>
#include <string_view>
#include <thread>

struct GLFWwindow;

class App {
public:
  App(int width, int height, std::string_view title);
  ~App();

  App(const App &) = delete;
  App &operator=(const App &) = delete;
  App(App &&) noexcept = delete;
  App &operator=(App &&) noexcept = delete;

  void run();

private:
  void initWindow(int width, int height, std::string_view title);
  void initVulkan();
  void cleanup();

  static void cursorPositionCallback(GLFWwindow *window, double xpos,
                                     double ypos);
  static void mouseButtonCallback(GLFWwindow *window, int button, int action,
                                  int mods);
  static void scrollCallback(GLFWwindow *window, double xoffset,
                             double yoffset);
  static void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                          int mods);
  static void framebufferResizeCallback(GLFWwindow *window, int width,
                                        int height);

  GLFWwindow *m_window = nullptr;
  VulkanContext m_vulkanContext;
  Camera m_camera;
  SimState m_simState;

  bool m_leftMouseDown = false;
  bool m_rightMouseDown = false;
  double m_lastMouseX = 0.0;
  double m_lastMouseY = 0.0;
  bool m_framebufferResized = false;

  std::atomic<bool> m_exportBusy{false};
  std::thread m_exportThread;
};
