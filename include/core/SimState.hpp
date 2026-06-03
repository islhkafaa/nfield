#pragma once

#include <cstdint>
struct SimState {
  float dt = 0.001f;
  float G = 1.0f;
  float softeningSqr = 0.01f;
  float theta = 0.5f;
  int numParticles = 8192;
  bool paused = false;

  enum class Algorithm { Direct, BarnesHut } algorithm = Algorithm::Direct;

  // Post-processing
  bool bloomEnabled = true;
  float bloomThreshold = 0.6f;
  float bloomIntensity = 0.8f;
  bool trailEnabled = false;
  float trailDecay = 0.88f;
  uint32_t colorMode = 0; // 0=velocity 1=mass 2=kinetic 3=rainbow

  // Diagnostics (written by VulkanContext each frame)
  float gpuComputeMs = 0.0f;
};
