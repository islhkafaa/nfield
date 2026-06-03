#include "core/Preset.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <random>

namespace {
// Utility to get tangential velocity for circular orbit
glm::vec3 getCircularVelocity(const glm::vec3 &pos, const glm::vec3 &center,
                              float centerMass, float G) {
  glm::vec3 rVec = pos - center;
  float r = glm::length(rVec);
  if (r < 0.01f)
    return glm::vec3(0.0f);

  float vMag = std::sqrt(G * centerMass / r);
  // Tangential vector in XZ plane (assuming Y is up)
  glm::vec3 tangent = glm::normalize(glm::vec3(-rVec.z, 0.0f, rVec.x));
  return tangent * vMag;
}
} // namespace

std::vector<Particle> generatePreset(PresetType type, uint32_t numParticles) {
  std::vector<Particle> particles(numParticles);
  std::mt19937 gen(42); // Seeded for reproducibility
  std::uniform_real_distribution<float> dis(0.0f, 1.0f);
  std::uniform_real_distribution<float> disSigned(-1.0f, 1.0f);
  std::normal_distribution<float> normal(0.0f, 1.0f);

  float G = 1.0f; // Standard G for initial velocity calculations

  switch (type) {
  case PresetType::RandomCloud: {
    // A uniform spherical distribution of particles with small velocities
    float radius = 12.0f;
    for (uint32_t i = 0; i < numParticles; ++i) {
      // Uniform volume distribution
      float r = radius * std::pow(dis(gen), 1.0f / 3.0f);
      float theta = std::acos(disSigned(gen));
      float phi = dis(gen) * glm::two_pi<float>();

      particles[i].pos =
          glm::vec4(r * std::sin(theta) * std::cos(phi),
                    r * std::sin(theta) * std::sin(phi), r * std::cos(theta),
                    1.0f // mass
          );

      // Small random velocity
      float speed = 0.5f;
      particles[i].vel = glm::vec4(normal(gen) * speed, normal(gen) * speed,
                                   normal(gen) * speed,
                                   0.0f // unused vel.w
      );
    }
    break;
  }

  case PresetType::GalaxyDisk: {
    // A rotating disk with a central massive core
    glm::vec3 center(0.0f);
    float coreMass = 4000.0f;

    // Core particle at index 0
    particles[0].pos = glm::vec4(center, coreMass);
    particles[0].vel = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    for (uint32_t i = 1; i < numParticles; ++i) {
      // Exponential disk distribution
      float u = dis(gen);
      float r = 1.5f + 18.0f * u * u; // denser towards center
      float phi = dis(gen) * glm::two_pi<float>();

      // Thick disk: small variation in Y height
      float y = normal(gen) * 0.4f * (1.0f - u);

      glm::vec3 pos(center.x + r * std::cos(phi), center.y + y,
                    center.z + r * std::sin(phi));

      glm::vec3 vel = getCircularVelocity(pos, center, coreMass, G);

      // Add minor velocity dispersion/turbulence
      vel.x += normal(gen) * 0.08f * glm::length(vel);
      vel.y += normal(gen) * 0.05f * glm::length(vel);
      vel.z += normal(gen) * 0.08f * glm::length(vel);

      particles[i].pos = glm::vec4(pos, 0.5f); // mass 0.5
      particles[i].vel = glm::vec4(vel, 0.0f);
    }
    break;
  }

  case PresetType::BinarySystem: {
    // Two massive bodies orbiting their barycenter, surrounded by a disk of
    // particles
    float starMass = 2500.0f;
    float orbitRadius = 6.0f;

    // Keplerian orbital velocity for each star orbiting barycenter
    float starSpeed = std::sqrt(G * starMass / (4.0f * orbitRadius));

    glm::vec3 pos1(-orbitRadius, 0.0f, 0.0f);
    glm::vec3 pos2(orbitRadius, 0.0f, 0.0f);

    glm::vec3 vel1(0.0f, 0.0f, -starSpeed);
    glm::vec3 vel2(0.0f, 0.0f, starSpeed);

    particles[0].pos = glm::vec4(pos1, starMass);
    particles[0].vel = glm::vec4(vel1, 0.0f);

    particles[1].pos = glm::vec4(pos2, starMass);
    particles[1].vel = glm::vec4(vel2, 0.0f);

    for (uint32_t i = 2; i < numParticles; ++i) {
      // Distribute particles in a large circumbinary disk or around individual
      // stars
      if (i % 2 == 0) {
        // Orbiting star 1
        float r = 1.0f + 2.5f * dis(gen);
        float phi = dis(gen) * glm::two_pi<float>();
        glm::vec3 posOffset(r * std::cos(phi), normal(gen) * 0.1f,
                            r * std::sin(phi));
        glm::vec3 pos = pos1 + posOffset;
        glm::vec3 vel = vel1 + getCircularVelocity(pos, pos1, starMass, G);
        particles[i].pos = glm::vec4(pos, 0.1f);
        particles[i].vel = glm::vec4(vel, 0.0f);
      } else {
        // Orbiting star 2
        float r = 1.0f + 2.5f * dis(gen);
        float phi = dis(gen) * glm::two_pi<float>();
        glm::vec3 posOffset(r * std::cos(phi), normal(gen) * 0.1f,
                            r * std::sin(phi));
        glm::vec3 pos = pos2 + posOffset;
        glm::vec3 vel = vel2 + getCircularVelocity(pos, pos2, starMass, G);
        particles[i].pos = glm::vec4(pos, 0.1f);
        particles[i].vel = glm::vec4(vel, 0.0f);
      }
    }
    break;
  }

  case PresetType::SolarSystem: {
    // A central star with particles in distinct orbital rings
    glm::vec3 center(0.0f);
    float sunMass = 8000.0f;

    particles[0].pos = glm::vec4(center, sunMass);
    particles[0].vel = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Ring radii representing planetary orbits
    float ringRadii[] = {3.0f, 5.5f, 8.0f, 11.0f, 14.5f, 18.0f};
    int numRings = 6;

    for (uint32_t i = 1; i < numParticles; ++i) {
      // Select a ring index
      int ringIdx = i % numRings;
      float baseR = ringRadii[ringIdx];

      // Add small radial perturbation and inclination
      float r = baseR + disSigned(gen) * 0.3f;
      float phi = dis(gen) * glm::two_pi<float>();
      float y = normal(gen) * 0.05f;

      glm::vec3 pos(center.x + r * std::cos(phi), center.y + y,
                    center.z + r * std::sin(phi));

      glm::vec3 vel = getCircularVelocity(pos, center, sunMass, G);

      // Give the planet orbits a tiny eccentricity
      vel.x += disSigned(gen) * 0.01f * glm::length(vel);
      vel.z += disSigned(gen) * 0.01f * glm::length(vel);

      // Planetary particles are relatively light
      particles[i].pos = glm::vec4(pos, 0.2f);
      particles[i].vel = glm::vec4(vel, 0.0f);
    }
    break;
  }

  case PresetType::CollidingGalaxies: {
    // Two galaxies on a collision course
    uint32_t halfN = numParticles / 2;

    // Galaxy 1: centered at (-12, 1, -12), moving towards center
    glm::vec3 center1(-12.0f, 1.0f, -12.0f);
    glm::vec3 drift1(2.5f, 0.0f, 2.5f);
    float mass1 = 2000.0f;

    particles[0].pos = glm::vec4(center1, mass1);
    particles[0].vel = glm::vec4(drift1, 0.0f);

    for (uint32_t i = 1; i < halfN; ++i) {
      float u = dis(gen);
      float r = 1.0f + 8.0f * u * u;
      float phi = dis(gen) * glm::two_pi<float>();
      float y = normal(gen) * 0.2f * (1.0f - u);

      glm::vec3 pos(center1.x + r * std::cos(phi), center1.y + y,
                    center1.z + r * std::sin(phi));

      glm::vec3 vel = drift1 + getCircularVelocity(pos, center1, mass1, G);
      particles[i].pos = glm::vec4(pos, 0.3f);
      particles[i].vel = glm::vec4(vel, 0.0f);
    }

    // Galaxy 2: centered at (12, -1, 12), moving towards center
    glm::vec3 center2(12.0f, -1.0f, 12.0f);
    glm::vec3 drift2(-2.5f, 0.0f, -2.5f);
    float mass2 = 2000.0f;

    particles[halfN].pos = glm::vec4(center2, mass2);
    particles[halfN].vel = glm::vec4(drift2, 0.0f);

    for (uint32_t i = halfN + 1; i < numParticles; ++i) {
      float u = dis(gen);
      float r = 1.0f + 8.0f * u * u;
      // Counter-rotating disk (note the negative phi tangential)
      float phi = dis(gen) * glm::two_pi<float>();
      float y = normal(gen) * 0.2f * (1.0f - u);

      glm::vec3 pos(center2.x + r * std::cos(phi), center2.y + y,
                    center2.z + r * std::sin(phi));

      // Reverse tangential velocity direction for counter-rotation
      glm::vec3 vel = drift2 - getCircularVelocity(pos, center2, mass2, G);
      particles[i].pos = glm::vec4(pos, 0.3f);
      particles[i].vel = glm::vec4(vel, 0.0f);
    }
    break;
  }
  }

  // Subtract center-of-mass velocity to prevent system drift
  if (type == PresetType::GalaxyDisk || type == PresetType::SolarSystem) {
    // Keep the central core (index 0) stationary at velocity 0.
    // Adjust the other particles to have zero net momentum.
    glm::vec3 diskMomentum(0.0f);
    float diskMass = 0.0f;
    for (size_t i = 1; i < particles.size(); ++i) {
      float m = particles[i].pos.w;
      if (m > 0.0f) {
        diskMomentum += glm::vec3(particles[i].vel) * m;
        diskMass += m;
      }
    }
    if (diskMass > 0.0f) {
      glm::vec3 vDrift = diskMomentum / diskMass;
      for (size_t i = 1; i < particles.size(); ++i) {
        particles[i].vel.x -= vDrift.x;
        particles[i].vel.y -= vDrift.y;
        particles[i].vel.z -= vDrift.z;
      }
    }
  } else {
    // Global adjustment for presets without a single stationary central body
    glm::vec3 totalMomentum(0.0f);
    float totalMass = 0.0f;
    for (const auto &p : particles) {
      float m = p.pos.w;
      if (m > 0.0f) {
        totalMomentum += glm::vec3(p.vel) * m;
        totalMass += m;
      }
    }
    if (totalMass > 0.0f) {
      glm::vec3 vDrift = totalMomentum / totalMass;
      for (auto &p : particles) {
        p.vel.x -= vDrift.x;
        p.vel.y -= vDrift.y;
        p.vel.z -= vDrift.z;
      }
    }
  }

  return particles;
}

const char *getPresetName(PresetType type) {
  switch (type) {
  case PresetType::RandomCloud:
    return "Random Sphere Cloud";
  case PresetType::GalaxyDisk:
    return "Rotating Disk Galaxy";
  case PresetType::BinarySystem:
    return "Binary Orbiting Stars";
  case PresetType::SolarSystem:
    return "Concentric Orbiting Rings";
  case PresetType::CollidingGalaxies:
    return "Colliding Galaxies";
  }
  return "Unknown";
}
