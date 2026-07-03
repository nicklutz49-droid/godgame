#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

// A villager: kinematic walking agent that only behaves like a rigid body
// while the hand throws it. Plain data, no GL. Villagers are never removed
// this slice (invulnerable), so indices into Village::villagers are stable.

enum class Job : std::uint8_t {
  None,
  Forester,
  Farmer,
  Fisherman,
  Builder,
  Worshipper,  // dances at the village center totem, generating mana
};

enum class VState : std::uint8_t {
  // voluntary life
  Idle,
  Wander,
  Chat,
  GoTo,      // walking a job leg (target set by the job planner)
  Work,      // chopping / tending / casting / hammering / picking up
  Haul,      // carrying a prop to storage or a site
  GoEat,
  Eat,
  GoHome,
  Sleep,
  // physical, hand- or physics-driven (the arbiter cannot exit these)
  Held,
  Airborne,
  Swim,
  Stunned,
  GetUp,
  Panic,
  Cower,
  // reserved: Worship, Dead
};

struct Villager {
  glm::vec3 pos{0.0f};
  glm::vec3 vel{0.0f};              // meaningful in Airborne/Swim
  glm::quat rot{1, 0, 0, 0};        // tumble while Airborne
  glm::vec3 angVel{0.0f};
  float yaw = 0.0f;                 // facing while grounded
  float scale = 1.0f;               // children start at tune::kChildScale

  Job job = Job::None;
  VState state = VState::Idle;
  float stateTimer = 0.0f;          // time remaining in timed states
  float thinkTimer = 0.0f;

  float hunger = 0.3f;              // 0..1
  float energy = 1.0f;              // 0..1
  float fear = 0.0f;                // hand-trauma memory, decays

  float walkPhase = 0.0f;           // drives procedural limb animation
  float workTimer = 0.0f;           // current swing/cast/etc. cycle
  int workCount = 0;                // swings done, catches made...

  glm::vec2 moveTarget{0.0f};
  int targetProp = -1;              // tree/log being worked (index into World::props)
  int blacklistProp = -1;           // recently failed target, skipped for a while
  float blacklistTimer = 0.0f;
  int carriedProp = -1;             // log/food in the arms
  int targetBuilding = -1;
  int targetCell = -1;              // farm cell
  int home = -1;                    // building index; -1 = homeless (campfire)

  bool alive = true;                // dead villagers keep their slot (indices
                                    // stay stable, like props and buildings)
  bool held = false;                // in the divine hand
  bool inside = false;              // sleeping inside the home (not rendered)
  bool pendingAssign = false;       // gently placed: resolve job on landing
  bool wasThrown = false;           // panic on recovery

  float stun = 0.0f;
  float submergedTime = 0.0f;       // drowning triggers past kDrownSeconds
  float starveTimer = 0.0f;         // day-fractions spent at hunger 1.0
  float progressTimer = 0.0f;       // stuck watchdog
  glm::vec2 lastProgressPos{0.0f};

  std::uint32_t rng = 1;            // per-villager XorShift stream
  int variant = 0;                  // skin/hair variant
  bool lookAtHand = false;          // render hint: head-track the hand
  float headLook = 0.0f;            // smoothed head yaw offset toward the hand
  float assignedFlash = 0.0f;       // brief highlight after drop-to-assign
  float danceAngle = 0.0f;          // worshipper's position on the totem ring
};
