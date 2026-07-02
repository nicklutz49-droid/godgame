#pragma once

#include <glm/glm.hpp>

#include "Mesh.h"
#include "Shader.h"

// Fullscreen gradient sky with a sun disc; drawn first with depth disabled.
// Day/night aware: zenith/sun colors come from the DayCycle, stars fade in
// after dark.
class Sky {
 public:
  void init();
  void draw(const glm::mat4& invViewProj, const glm::vec3& camPos,
            const glm::vec3& sunDir, const glm::vec3& fogColor,
            const glm::vec3& zenithColor, const glm::vec3& sunColor,
            float night);

 private:
  Mesh mesh_;
  Shader shader_;
};
