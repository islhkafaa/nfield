#include "core/Export.hpp"
#include <fstream>
#include <stdexcept>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

void exportParticlesCSV(const std::vector<Particle> &particles,
                        const std::string &path) {
  std::ofstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }

  file << "x,y,z,mass,vx,vy,vz\n";
  for (const auto &p : particles) {
    file << p.pos.x << "," << p.pos.y << "," << p.pos.z << "," << p.pos.w << ","
         << p.vel.x << "," << p.vel.y << "," << p.vel.z << "\n";
  }
}

void exportParticlesBinary(const std::vector<Particle> &particles,
                           const std::string &path) {
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }

  uint32_t count = static_cast<uint32_t>(particles.size());
  file.write(reinterpret_cast<const char *>(&count), sizeof(count));
  file.write(reinterpret_cast<const char *>(particles.data()),
             count * sizeof(Particle));
}
