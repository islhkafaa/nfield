#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
  Camera() = default;

  void setAspect(float aspect) { m_aspect = aspect; }

  void zoom(float offset) {
    m_distance = std::clamp(m_distance - offset, 1.0f, 500.0f);
  }

  void rotate(float deltaYaw, float deltaPitch) {
    m_yaw += deltaYaw;
    m_pitch = std::clamp(m_pitch + deltaPitch, -glm::half_pi<float>() + 0.01f,
                         glm::half_pi<float>() - 0.01f);
  }

  void pan(float deltaX, float deltaY) {
    glm::vec3 forward = glm::normalize(m_target - getPosition());
    glm::vec3 right =
        glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::cross(right, forward);

    m_target += right * deltaX + up * deltaY;
  }

  glm::vec3 getTarget() const { return m_target; }

  glm::vec3 getPosition() const {
    float cosPitch = glm::cos(m_pitch);
    glm::vec3 offset(m_distance * glm::sin(m_yaw) * cosPitch,
                     m_distance * glm::sin(m_pitch),
                     m_distance * glm::cos(m_yaw) * cosPitch);
    return m_target + offset;
  }

  glm::mat4 getViewMatrix() const {
    return glm::lookAt(getPosition(), m_target, glm::vec3(0.0f, 1.0f, 0.0f));
  }

  glm::mat4 getProjectionMatrix() const {
    auto proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
    proj[1][1] *= -1.0f;
    return proj;
  }

  glm::mat4 getViewProjectionMatrix() const {
    return getProjectionMatrix() * getViewMatrix();
  }

private:
  glm::vec3 m_target = glm::vec3(0.0f);
  float m_distance = 25.0f;
  float m_pitch = 0.5f;
  float m_yaw = 0.0f;

  float m_fov = 45.0f;
  float m_aspect = 16.0f / 9.0f;
  float m_near = 0.1f;
  float m_far = 1000.0f;
};
