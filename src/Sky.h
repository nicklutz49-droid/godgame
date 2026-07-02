#pragma once

#include <glm/glm.hpp>

#include "Mesh.h"
#include "Shader.h"

// Fullscreen gradient sky with a sun disc; drawn first with depth disabled.
class Sky {
 public:
  void init();
  void draw(const glm::mat4& invViewProj, const glm::vec3& camPos,
            const glm::vec3& sunDir, const glm::vec3& fogColor);

 private:
  Mesh mesh_;
  Shader shader_;
};
