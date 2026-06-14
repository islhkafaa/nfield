#pragma once

#include "vulkan/VulkanContext.hpp"
#include <string>
#include <vector>

void exportParticlesCSV(const std::vector<Particle> &particles,
                        const std::string &path);
void exportParticlesBinary(const std::vector<Particle> &particles,
                           const std::string &path);
