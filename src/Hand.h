#pragma once

#include <glm/glm.hpp>

#include "World.h"

// The divine hand: follows the cursor across the terrain, hovers over
// pickable props, carries one at a time and throws it with the velocity the
// player imparted while dragging.
class Hand {
 public:
  enum class Mode { Free, Carry };

  Mode mode = Mode::Free;
  int heldProp = -1;
  int hoverProp = -1;
  glm::vec3 pos{0.0f};        // rendered hand position
  glm::vec3 groundPoint{0.0f};  // where the cursor ray meets terrain/water
  bool hasGround = false;

  void update(float dt, const glm::vec3& rayOrigin, const glm::vec3& rayDir,
              World& world);

  // Grab whatever is hovered; returns true if a prop was taken.
  bool tryGrab(World& world);
  void release(World& world);

 private:
  glm::vec3 throwVel_{0.0f};
  bool hasPrevHeldPos_ = false;
  glm::vec3 prevHeldPos_{0.0f};
};
