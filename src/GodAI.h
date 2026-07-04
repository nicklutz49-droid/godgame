#pragma once

#include <glm/glm.hpp>

#include <cstdint>

#include "Village.h"
#include "Villagers.h"

class World;

// The rival god's mind and its embodied hand (M5). It plays only through the
// player's verbs - grab/carry/release, drop-to-assign, scaffold combining and
// placement, miracles, gift throws - one order at a time, rate-limited by
// travel speed and cooldowns rather than restrained by cheats. Sim-only
// (no GL) and deterministic: one XorShift stream, a fixed think cadence,
// updated in god-index order from World::update.
class GodAI {
 public:
  // What the hand is doing right now (drives the render ghost too).
  enum class Phase : std::uint8_t { Rest, ToPickup, ToTarget };
  enum class Verb : std::uint8_t {
    None,
    Devote,   // carry an idle adult to the totem (drop-to-assign)
    Feed,     // food miracle over an owned, hungry village
    Build,    // place a scaffold stack (the governor's growth plan)
    Combine,  // merge scaffold stacks toward the plan's count
    Gift,     // haul or hurl a resource onto another village's pad
    Court,    // food miracle at a village it wants to win over
    Rain,     // shower dry fields in an owned village (M11)
    Smite,    // CRUEL only: a fireball on a reachable enemy village (M11)
  };

  int god = 1;
  int profile = 1;  // index into tune::kAiProfiles (EASY/FAIR/CRUEL)
  glm::vec3 handPos{0.0f, 1.0e9f, 0.0f};
  glm::vec3 handVel{0.0f};  // smoothed; villagers judge the swoop by it
  Phase phase = Phase::Rest;
  Verb verb = Verb::None;
  GrabTarget held;  // carried villager or prop (render + re-validation)

  // Observable behavior counters (tests now, HUD later).
  int devotions = 0, feeds = 0, placements = 0, combines = 0, gifts = 0,
      courtCasts = 0, rainCasts = 0, smites = 0;

  void reset(const World& world, int godIdx);
  void update(World& world, float dt);

  // Put down whatever the hand carries and go to Rest - game saves settle
  // every hand so a save is always a valid world (M7).
  void settle(World& world);

 private:
  void think(World& world);
  bool orderGiftRun(World& world, int villageIdx);
  void moveToward(World& world, const glm::vec3& dest, float dt);
  bool arrived(const glm::vec3& dest) const;
  void carryHeld(World& world, float dt);
  bool heldStillValid(const World& world) const;
  void executeGrab(World& world);
  void executeRelease(World& world);
  void abandon(World& world);
  glm::vec3 restPoint(const World& world) const;

  glm::vec3 pickup_{0.0f};
  glm::vec3 target_{0.0f};
  int targetVillage_ = -1;   // village the order acts on / courts
  int mergeInto_ = -1;       // Combine: the stack that receives the held one
  BuildingType civic_ = BuildingType::Store;
  bool throwIt_ = false;     // Gift: hurl from range instead of hand-delivery
  float thinkTimer_ = 0.0f;
  float cooldown_ = 0.0f;
  std::uint32_t rng_ = 1;

  friend struct SaveIO;  // full-state game saves restore privates (M7)
};
