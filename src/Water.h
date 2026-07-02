#pragma once

#include <glm/glm.hpp>

#include "Mesh.h"
#include "Shader.h"

// Animated transparent ocean plane surrounding the island.
class Water {
 public:
  void init();
  void draw(const glm::mat4& viewProj, const glm::vec3& camPos,
            const glm::vec3& sunDir, const glm::vec3& fogColor,
            float fogDensity, float time);

 private:
  Mesh mesh_;
  Shader shader_;
};
