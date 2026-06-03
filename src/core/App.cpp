#include "core/App.hpp"
#include "core/Preset.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
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
  if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
    return;
  }
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
  if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
    return;
  }
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
  if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
    return;
  }
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->m_camera.zoom(static_cast<float>(yoffset * 2.0));
  }
}

void App::run() {
  float lastTime = static_cast<float>(glfwGetTime());

  float fpsTimer = 0.0f;
  int fpsFrameCount = 0;
  float currentFPS = 0.0f;
  float currentFrameTimeMs = 0.0f;

  PresetType selectedPreset = PresetType::RandomCloud;
  int selectedParticleCount = 8192;

  while (!glfwWindowShouldClose(m_window)) {
    glfwPollEvents();

    float currentTime = static_cast<float>(glfwGetTime());
    float dt = currentTime - lastTime;
    lastTime = currentTime;

    fpsTimer += dt;
    fpsFrameCount++;
    if (fpsTimer >= 1.0f) {
      currentFPS = static_cast<float>(fpsFrameCount) / fpsTimer;
      currentFrameTimeMs = (fpsTimer / fpsFrameCount) * 1000.0f;
      fpsTimer = 0.0f;
      fpsFrameCount = 0;
    }

    if (dt > 0.1f) {
      dt = 0.1f;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Nfield Control Panel", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
      if (ImGui::CollapsingHeader("Simulation Controls",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Pause Simulation", &m_simState.paused);

        ImGui::SliderFloat("Time Step (dt)", &m_simState.dt, 0.0001f, 0.01f,
                           "%.4f");
        ImGui::SliderFloat("Gravitational Constant (G)", &m_simState.G, 0.1f,
                           10.0f, "%.2f");
        ImGui::SliderFloat("Softening Factor", &m_simState.softeningSqr, 0.001f,
                           1.0f, "%.3f");

        int currentAlg = static_cast<int>(m_simState.algorithm);
        ImGui::Text("Algorithm:");
        ImGui::RadioButton("Direct O(N²)", &currentAlg, 0);
        ImGui::RadioButton("Barnes-Hut O(N log N)", &currentAlg, 1);
        m_simState.algorithm = static_cast<SimState::Algorithm>(currentAlg);

        if (m_simState.algorithm == SimState::Algorithm::BarnesHut) {
          ImGui::SliderFloat("Theta (BH parameter)", &m_simState.theta, 0.1f,
                             1.5f, "%.2f");
        }
      }

      if (ImGui::CollapsingHeader("Particles & Initial Conditions",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        const int particleCounts[] = {512,   1024,  4096, 8192,
                                      16384, 32768, 65536};
        const char *countStrings[] = {"512",    "1,024",  "4,096", "8,192",
                                      "16,384", "32,768", "65,536"};

        int currentCountIdx = 3;
        for (int i = 0; i < 7; ++i) {
          if (particleCounts[i] == selectedParticleCount) {
            currentCountIdx = i;
            break;
          }
        }

        if (ImGui::Combo("Particle Count", &currentCountIdx, countStrings, 7)) {
          selectedParticleCount = particleCounts[currentCountIdx];
        }

        const PresetType presetTypes[] = {
            PresetType::RandomCloud, PresetType::GalaxyDisk,
            PresetType::BinarySystem, PresetType::SolarSystem,
            PresetType::CollidingGalaxies};
        const char *presetNames[] = {
            "Random Sphere Cloud", "Rotating Disk Galaxy",
            "Binary Orbiting Stars", "Concentric Orbiting Rings",
            "Colliding Galaxies"};

        int currentPresetIdx = 0;
        for (int i = 0; i < 5; ++i) {
          if (presetTypes[i] == selectedPreset) {
            currentPresetIdx = i;
            break;
          }
        }

        if (ImGui::Combo("Select Preset", &currentPresetIdx, presetNames, 5)) {
          selectedPreset = presetTypes[currentPresetIdx];
        }

        if (ImGui::Button("Reset & Load Initial State", ImVec2(-1, 0))) {
          std::vector<Particle> newParticles =
              generatePreset(selectedPreset, selectedParticleCount);
          m_simState.numParticles = selectedParticleCount;
          m_vulkanContext.reloadParticles(newParticles);
        }
      }

      if (ImGui::CollapsingHeader("Rendering",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Bloom", &m_simState.bloomEnabled);
        if (m_simState.bloomEnabled) {
          ImGui::SliderFloat("Bloom Threshold", &m_simState.bloomThreshold,
                             0.1f, 1.5f, "%.2f");
          ImGui::SliderFloat("Bloom Intensity", &m_simState.bloomIntensity,
                             0.0f, 3.0f, "%.2f");
        }
        ImGui::Separator();
        ImGui::Checkbox("Particle Trails", &m_simState.trailEnabled);
        if (m_simState.trailEnabled) {
          ImGui::SliderFloat("Trail Decay", &m_simState.trailDecay, 0.5f, 0.99f,
                             "%.3f");
        }
        ImGui::Separator();
        const char *colorModes[] = {"Velocity", "Mass", "Kinetic Energy",
                                    "Rainbow"};
        int colorMode = static_cast<int>(m_simState.colorMode);
        if (ImGui::Combo("Color Mode", &colorMode, colorModes, 4)) {
          m_simState.colorMode = static_cast<uint32_t>(colorMode);
        }
      }

      if (ImGui::CollapsingHeader("Performance Metrics",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", currentFPS);
        ImGui::Text("CPU Frame: %.2f ms", currentFrameTimeMs);
        if (m_simState.gpuComputeMs > 0.0f) {
          ImGui::Text("GPU Compute: %.3f ms", m_simState.gpuComputeMs);
        } else {
          ImGui::TextDisabled("GPU Compute: N/A");
        }
        ImGui::Text("Bodies: %d", m_simState.numParticles);
      }
    }
    ImGui::End();

    ImGui::Render();

    m_vulkanContext.drawFrame(m_camera.getViewProjectionMatrix(), m_simState);
  }

  if (m_vulkanContext.getDevice() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_vulkanContext.getDevice());
  }
}
