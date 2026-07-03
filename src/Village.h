#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "Noise.h"
#include "Villager.h"

class World;

enum class BuildingType : std::uint8_t {
  Center,     // the totem - worship anchor; upgradable by 5-scaffold stacks
  Storage,    // the open stock pad (not the civic Store)
  Campfire,
  House,      // small abode (1 scaffold, or the autonomous fallback path)
  LargeAbode, // 2 scaffolds
  Store,      // 3 - raises storage caps
  Workshop,   // 3 - crafts scaffolds from wood
  Creche,     // 3 - eases births
  Graveyard,  // 3 - symbolic until mortality (M2): small belief sustain
  FieldSite,  // 4 - becomes a farm plot on completion
  Dispenser,  // 6 - stores miracle charges from mana overflow
  Wonder,     // 7 - awe aura
  // reserved: Grave
};

struct Building {
  BuildingType type = BuildingType::House;
  glm::vec3 pos{0.0f};
  float yaw = 0.0f;
  // stage -1 = reserved plot (invisible), 0..2 = construction, 3 = complete.
  int stage = 3;
  int woodCost = 0;        // autonomous fallback path hauls this much first
  int woodDelivered = 0;
  float buildProgress = 0.0f;  // hammer-seconds accumulated
  int residents = 0;
  int tier = 0;            // scaffold count that created it (0 = legacy path)
  int level = 1;           // Center upgrades: 1..3
  int charges = 0;         // Dispenser: stored miracle casts
};

// A farm plot (the founding one plus any player-placed Fields).
struct Field {
  glm::vec2 center{0.0f};
  glm::vec2 half{8.0f, 5.0f};
};

struct FarmCell {
  glm::vec2 pos{0.0f};
  float growth = 0.0f;      // 0..1, harvest at 1
  float tendedTimer = 0.0f; // recently tended -> grows faster
  int claimedBy = -1;
};

// The one settlement: buildings, fields, stores, and its people.
// Sim-only (no GL); everything deterministic per seed.
class Village {
 public:
  bool founded = false;
  glm::vec3 center{0.0f};
  float radius = 32.0f;          // flattened terrace / footprint radius

  // Faith in the player, 0..1. Raised by witnessed divine acts, sustained by
  // worship, decaying toward a floor. Scales worship mana output and the
  // village's influence ring.
  float belief = 0.25f;

  int wood = 0;
  int food = 0;
  // Cumulative counters for the headless soak asserts.
  int woodProduced = 0;
  int foodProduced = 0;
  int mealsEaten = 0;
  int stuckEvents = 0;
  int scaffoldsCrafted = 0;
  int deaths = 0;
  int burials = 0;

  std::vector<Building> buildings;
  std::vector<Field> fields;
  std::vector<FarmCell> farmCells;
  std::vector<glm::vec3> fishingSpots;
  std::vector<Villager> villagers;

  int centerIdx = -1, storageIdx = -1, campfireIdx = -1;
  float dispenserFill = 0.0f;  // mana overflow accumulating toward a charge

  // Founding: site selection (3-pass relax-then-terraform, never regenerate),
  // terrain flattening, building/field/fishing layout. Runs inside
  // World::generate between terrain gen and prop scatter.
  void plan(World& world, std::uint32_t seed);
  void spawnVillagers(World& world, std::uint32_t seed);

  // Per-frame settlement step: crop growth, construction stage advance,
  // opening new sites, and the dawn tick (population growth).
  void step(World& world, float dt);

  // Drop-to-assign: what job does a gentle placement at p mean?
  // Priority: construction site > field > tree > water/shore. Job::None = no change.
  Job resolveJobAtPoint(const World& world, const glm::vec3& p) const;

  bool inStorageRadius(const glm::vec3& p) const;
  bool insideFootprint(float x, float z) const;  // prop-scatter keep-out
  bool insideAnyField(float x, float z, float margin) const;
  int housingCapacity() const;
  int population() const;  // the living only

  // Lay a body to rest: consumes the prop, digs a grave at the (completed)
  // graveyard, restores a little belief. Returns false without a graveyard
  // in range.
  bool buryBody(World& world, int propIdx);
  int completedGraveyard() const;  // building index or -1

  // Storage caps (base + completed Stores). Deposits pause when full.
  int foodCap() const;
  int woodCap() const;

  // What a scaffold stack of `count` builds (civicChoice picks among the
  // 3-stack civic buildings).
  static BuildingType buildingForStack(int count, BuildingType civicChoice);

  // Everything that happens the moment a construction finishes: beds claimed,
  // caps raised, fields planted, center leveled, auras armed.
  void onBuildingComplete(World& world, int buildingIdx);

  // Worship multiplier / influence bonus from the Center's upgrade level.
  float centerManaMultiplier() const;
  float centerLevel() const;

  // Wonder aura test (belief decay & witness awe).
  bool insideWonderAura(const glm::vec3& p) const;

  int countCompleted(BuildingType type) const;

  // Append a farm plot (founding layout and completed FieldSites).
  void addField(glm::vec2 center2, glm::vec2 half, noise::XorShift* rng);

  // Absorb a resource prop (log/food/tree) into the stores.
  void absorbProp(World& world, int propIdx);

  // Witness bus for divine acts. Villagers within 30 m gain `fear`; belief
  // rises by `awe` scaled by how much of the village saw it.
  void notifyDivineEvent(const glm::vec3& where, float fear, float awe);

  // How far the hand's power extends around this village (grows with belief).
  float influenceRadius() const;

  // Villagers currently dancing at the totem (for render glow / stats).
  int activeWorshippers() const;

  glm::vec3 storagePos() const;
  glm::vec3 campfirePos() const;

  int findHomeFor(int villagerIdx);  // claim a bed, or -1

  // Mortality seam (stub, unreachable while invulnerable): free the bed,
  // later spawn a body prop / grave and notify belief.
  void onVillagerDeath(int villagerIdx);

 private:
  float lastT = -1.0f;  // detects the dawn crossing
  std::uint32_t rng_ = 1;
};
