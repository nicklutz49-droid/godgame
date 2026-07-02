#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

#include "DayCycle.h"
#include "Terrain.h"
#include "Village.h"

enum class PropType : std::uint8_t {
  Rock,
  Tree,
  Log,    // felled wood, haulable, floats
  Food,   // meal bundle (grain/fish), haulable, floats
  Stump,  // what remains of a chopped tree
  // reserved: Body - mortality slice (corpses are just grabbable props)
};

// A physical object the hand can pick up and throw. Collision against the
// world is a single sphere vs the terrain heightfield - props do not collide
// with each other in this slice.
//
// Props are never erased mid-session: consumed props set alive=false and the
// slot is reused by spawnProp, so indices held by the hand and by villagers
// never dangle (holders re-validate alive/type on use).
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
  bool alive = true;
  bool held = false;     // in the divine hand
  bool asleep = true;
  bool uprighting = false;
  bool felled = false;   // chopped tree: falls for real and must not replant
  float restTimer = 0.0f;
  float resource = 0.0f; // trees: chop work remaining; food: meals it grants
  int claimedBy = -1;    // villager index working this prop
  int carrier = -1;      // villager index carrying this prop
};

// The god's seat of power: stands apart from any village, stores the mana
// pool that worship fills and miracles spend, and projects the base
// influence ring.
struct Temple {
  bool founded = false;
  glm::vec3 pos{0.0f};
  float yaw = 0.0f;
  float mana = 0.0f;
  float manaMax = 100.0f;
};

class World {
 public:
  // Mesh-space half height of the tree model; trees plant so the trunk base
  // touches the ground.
  static constexpr float kTreeHalfHeight = 3.25f;

  Terrain terrain;
  std::vector<Prop> props;
  Village village;
  Temple temple;
  DayCycle dayCycle;

  // The divine hand, as the sim sees it (set by the app / test harness each
  // frame; villagers react to it). Defaults far away and harmless.
  glm::vec3 handPos{0.0f, 1.0e9f, 0.0f};
  float handSpeed = 0.0f;

  void generate(std::uint32_t seed);
  void update(float dt);

  // Nearest prop whose (slightly enlarged) sphere the ray hits, or -1.
  int pickProp(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;

  void throwProp(int index, const glm::vec3& velocity);

  float restHeight(const Prop& p) const;  // y for the prop sitting on land

  // Reuses a dead slot when possible; returns the prop's index.
  int spawnProp(const Prop& p);

  // Is this point within the god's reach (temple ring or any village ring)?
  bool insideInfluence(const glm::vec3& p) const;

  // Rain food from the sky at p. Fails (returns false) outside influence or
  // with insufficient mana. Deterministic per cast via its own stream.
  bool castFoodMiracle(const glm::vec3& p);

  std::uint32_t seed() const { return seed_; }

 private:
  void foundTemple();
  void scatterProps();
  std::uint32_t seed_ = 1;
  std::uint32_t miracleCounter_ = 0;
};
