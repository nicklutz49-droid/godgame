#pragma once

#include <glm/glm.hpp>

#include "Villagers.h"
#include "World.h"

// The divine hand: follows the cursor across the terrain, hovers over
// pickable props AND villagers, carries one at a time, and throws it with the
// velocity the player imparted while dragging. A slow release is a gentle
// placement - for villagers that triggers drop-to-assign, for resources over
// the storage pad it deposits them.
class Hand {
 public:
  enum class Mode { Free, Carry };

  Mode mode = Mode::Free;
  GrabTarget held;
  GrabTarget hover;
  glm::vec3 pos{0.0f};          // rendered hand position
  glm::vec3 groundPoint{0.0f};  // where the cursor ray meets terrain/water
  bool hasGround = false;
  float lastReleaseSpeed = 0.0f;  // for threshold tuning (logged by the app)
  // While holding a 3-stack, the wheel cycles which civic building it becomes.
  BuildingType civicChoice = BuildingType::Store;

  // Is the hand carrying a scaffold of `count` stacks? (drives ghost preview
  // and the wheel intercept; count 0 = not a scaffold)
  int heldScaffoldCount(const World& world) const;

  void update(float dt, const glm::vec3& rayOrigin, const glm::vec3& rayDir,
              World& world);

  // Grab whatever is hovered; returns true if something was taken.
  bool tryGrab(World& world);
  void release(World& world);

 private:
  glm::vec3 throwVel_{0.0f};
  bool hasPrevHeldPos_ = false;
  glm::vec3 prevHeldPos_{0.0f};
};
