#pragma once

#include "vulkan/VulkanContext.hpp"
#include <vector>

enum class PresetType {
  RandomCloud,
  GalaxyDisk,
  BinarySystem,
  SolarSystem,
  CollidingGalaxies
};

std::vector<Particle> generatePreset(PresetType type, uint32_t numParticles);
const char *getPresetName(PresetType type);
