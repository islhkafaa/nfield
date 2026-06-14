#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct SimState {
  float dt = 0.001f;
  float G = 1.0f;
  float softeningSqr = 0.01f;
  float theta = 0.5f;
  int numParticles = 8192;
  bool paused = false;
  bool singleStep = false;

  enum class Algorithm { Direct, BarnesHut } algorithm = Algorithm::Direct;

  bool bloomEnabled = true;
  float bloomThreshold = 0.6f;
  float bloomIntensity = 0.8f;
  bool trailEnabled = false;
  float trailDecay = 0.88f;
  uint32_t colorMode = 0;

  float gpuComputeMs = 0.0f;
  float totalKineticEnergy = 0.0f;
  float totalMomentum = 0.0f;

  bool recordingFrames = false;

  bool spawnMode = false;
  float spawnMass = 1.0f;
  float spawnSpeed = 0.0f;
  glm::vec3 spawnVelocityDir = {0.0f, 1.0f, 0.0f};
};
