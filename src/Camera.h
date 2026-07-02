#pragma once

#include <glm/glm.hpp>

// Black & White style camera: orbits a focus point on the ground, pitch is
// coupled to zoom (overhead when far, near-horizon when close), and panning
// is done by "grabbing" the land so the point under the cursor stays put.
class Camera {
 public:
  glm::vec3 focus{0.0f, 5.0f, 40.0f};
  float yaw = 0.0f;             // radians around +Y
  float distance = 160.0f;
  float pitchOffset = 0.0f;     // user adjustment on top of zoom-coupled pitch
  float groundLift = 0.0f;      // set by App to keep the eye above terrain

  static constexpr float kMinDistance = 6.0f;
  static constexpr float kMaxDistance = 380.0f;

  float pitch() const;
  glm::vec3 position() const;
  glm::mat4 view() const;
  glm::mat4 proj(float aspect) const;

  // World-space ray direction through a window pixel.
  glm::vec3 rayDir(float px, float py, int width, int height) const;

  // Positive steps zoom in. If zoomTarget is given, the focus is pulled
  // toward it so the point under the cursor roughly holds its screen position.
  void zoomBy(float steps, const glm::vec3* zoomTarget);

  void orbit(float dxPixels, float dyPixels);
};
