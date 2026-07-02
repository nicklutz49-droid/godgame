#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

#include "Terrain.h"

enum class PropType { Rock, Tree };

// A physical object the hand can pick up and throw. Collision against the
// world is a single sphere vs the terrain heightfield - props do not collide
// with each other in this slice.
struct Prop {
  PropType type = PropType::Rock;
  int variant = 0;
  glm::vec3 pos{0.0f};
  glm::vec3 vel{0.0f};
  glm::quat rot{1, 0, 0, 0};
  glm::vec3 angVel{0.0f};
  float scale = 1.0f;
  float radius = 1.0f;   // collision sphere
  float baseYaw = 0.0f;  // trees replant facing their original direction
  bool held = false;
  bool asleep = true;
  bool uprighting = false;
  float restTimer = 0.0f;
};

class World {
 public:
  // Mesh-space half height of the tree model; trees plant so the trunk base
  // touches the ground.
  static constexpr float kTreeHalfHeight = 3.25f;

  Terrain terrain;
  std::vector<Prop> props;

  void generate(std::uint32_t seed);
  void update(float dt);

  // Nearest prop whose (slightly enlarged) sphere the ray hits, or -1.
  int pickProp(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;

  void throwProp(int index, const glm::vec3& velocity);

  float restHeight(const Prop& p) const;  // y for the prop sitting on land

 private:
  void scatterProps();
  std::uint32_t seed_ = 1;
};
