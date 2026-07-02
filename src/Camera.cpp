#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

float Camera::pitch() const {
  float t = (std::log(distance) - std::log(kMinDistance)) /
            (std::log(kMaxDistance) - std::log(kMinDistance));
  t = std::clamp(t, 0.0f, 1.0f);
  float base = glm::mix(glm::radians(16.0f), glm::radians(65.0f), t);
  return std::clamp(base + pitchOffset, glm::radians(8.0f), glm::radians(84.0f));
}

glm::vec3 Camera::position() const {
  glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
  float p = pitch();
  glm::vec3 pos = focus - forward * (std::cos(p) * distance) +
                  glm::vec3(0, 1, 0) * (std::sin(p) * distance);
  pos.y += groundLift;
  return pos;
}

glm::mat4 Camera::view() const {
  return glm::lookAt(position(), focus, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::proj(float aspect) const {
  return glm::perspective(glm::radians(55.0f), aspect, 0.5f, 2500.0f);
}

glm::vec3 Camera::rayDir(float px, float py, int width, int height) const {
  float x = 2.0f * px / static_cast<float>(width) - 1.0f;
  float y = 1.0f - 2.0f * py / static_cast<float>(height);
  float aspect = static_cast<float>(width) / static_cast<float>(height);
  glm::mat4 inv = glm::inverse(proj(aspect) * view());
  glm::vec4 nearP = inv * glm::vec4(x, y, -1.0f, 1.0f);
  glm::vec4 farP = inv * glm::vec4(x, y, 1.0f, 1.0f);
  return glm::normalize(glm::vec3(farP) / farP.w - glm::vec3(nearP) / nearP.w);
}

void Camera::zoomBy(float steps, const glm::vec3* zoomTarget) {
  float f = std::pow(0.87f, steps);
  float newDistance = std::clamp(distance * f, kMinDistance, kMaxDistance);
  float applied = newDistance / distance;
  if (zoomTarget && applied < 1.0f) {
    float pull = 1.0f - applied;
    focus.x = glm::mix(focus.x, zoomTarget->x, pull);
    focus.z = glm::mix(focus.z, zoomTarget->z, pull);
  }
  distance = newDistance;
}

void Camera::orbit(float dxPixels, float dyPixels) {
  yaw += dxPixels * 0.006f;
  pitchOffset = std::clamp(pitchOffset + dyPixels * 0.005f, -1.1f, 1.1f);
}
