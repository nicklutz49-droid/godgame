#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

#include "DayCycle.h"
#include "GodAI.h"
#include "Terrain.h"
#include "Village.h"

enum class PropType : std::uint8_t {
  Rock,
  Tree,
  Log,      // felled wood, haulable, floats
  Food,     // meal bundle (grain/fish), haulable, floats
  Stump,    // what remains of a chopped tree
  Scaffold, // crafted at workshops; resource = stack count (1..7); floats
  Body,     // a dead villager; carry it to the graveyard (it floats, grimly)
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
  float age = 0.0f;      // seconds since spawn (bodies rot past kCorpseRotDays)
  int claimedBy = -1;    // packed villager id working this prop
  int carrier = -1;      // packed villager id carrying this prop
  int thrownByGod = -1;  // gifts remember their sender until received
};

// A god's seat of power: stands apart from any village and projects the base
// influence ring.
struct Temple {
  bool founded = false;
  glm::vec3 pos{0.0f};
  float yaw = 0.0f;
};

// A god: the player (id 0) or a rival (M5). Owns a temple and the mana pool
// that its villages' worship fills and its miracles spend. `ai` hands the
// god to a GodAI brain (rivals always; the player only in AI-vs-AI tests).
struct God {
  bool active = false;
  bool isPlayer = false;
  bool ai = false;
  Temple temple;
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
  // villages[0] is the player's home village (owner 0); the rest start
  // neutral (owner -1). Indices are stable for the whole session.
  std::vector<Village> villages;
  God gods[tune::kMaxGods];
  GodAI ai[tune::kMaxGods];  // brains for gods with the ai flag set
  DayCycle dayCycle;

  Village& home() { return villages[0]; }
  const Village& home() const { return villages[0]; }

  // The divine hand, as the sim sees it (set by the app / test harness each
  // frame; villagers react to it). Defaults far away and harmless.
  glm::vec3 handPos{0.0f, 1.0e9f, 0.0f};
  float handSpeed = 0.0f;

  // godCount 1 = the peaceful sandbox; 2 = a skirmish world (the rival god
  // founds its own home village and temple on the site farthest from yours).
  void generate(std::uint32_t seed, int godCount = 1);
  void update(float dt);

  // --- editor verbs (M6): the author's hand. Sim-side, deterministic,
  // reusing the founding paths - maps never fork world-building logic. ---

  // A blank flat island: no villages, no props, the player god waiting.
  void buildBlank(std::uint32_t seed);

  // Activate a god (idempotent): starter mana, AI flag for rivals.
  void wakeGod(int god);

  // Found a village through the standard path (plan + spawn + preset stores
  // + god wake). terraform=false is the map-load path: the heightfield
  // already carries the terraces. No placement checks - the spec is truth.
  int foundVillageFromSpec(glm::vec2 site, int owner, int preset, bool terraform);

  // Found a village at `site` (owner -1 neutral / god id; preset 0..2 =
  // small/medium/large population and stores). Requires land and
  // kEditorVillageSeparation from existing villages; returns index or -1.
  // Owning gods wake (rivals with their AI).
  int editorPlaceVillage(glm::vec2 site, int owner, int preset);

  // Seat (or move) `god`'s temple: flattens a pad, faces the god's nearest
  // village, and wakes the god.
  void editorPlaceTemple(int god, glm::vec2 pos);

  // Plant a deterministic cluster of trees / rocks under the brush
  // (collision-checked against props, villages, temples).
  void editorPaintForest(glm::vec2 center, float radius);
  void editorPaintRocks(glm::vec2 center, float radius);

  // Remove trees/rocks/stumps under the brush; returns how many.
  int editorEraseProps(glm::vec2 center, float radius);

  // Re-seat props, buildings, and villagers on freshly sculpted ground.
  void editorSnapToGround(glm::vec2 center, float radius);

  // An active god with no villages left is broken: its AI goes still and its
  // worship income is gone. (The temple-collapse ceremony arrives with M7.)
  bool godBroken(int god) const;

  // Nearest prop whose (slightly enlarged) sphere the ray hits, or -1.
  int pickProp(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;

  void throwProp(int index, const glm::vec3& velocity);

  float restHeight(const Prop& p) const;  // y for the prop sitting on land

  // Reuses a dead slot when possible; returns the prop's index.
  int spawnProp(const Prop& p);

  // Is this point within a god's reach (their temple ring or any village
  // they own)? Neutral villages project nothing.
  bool insideInfluence(const glm::vec3& p, int god = 0) const;

  // Divine acts ripple to every village whose people can see them; the awe
  // credits the acting god's standing there.
  void notifyDivineEvent(int god, const glm::vec3& where, float fear, float awe);

  // The conversion ratchet, checked continuously: neutrals join a clearly
  // leading god; owned villages flip only to overwhelming faith over a
  // lapsed owner. Flips are ceremonies (scatter, fear, suppressed rivals).
  void updateOwnership();
  void convertVillage(int villageIdx, int newOwner);

  // Static-obstacle spatial grid (trees/rocks/stumps), rebuilt each update;
  // steering queries it instead of scanning every prop.
  void rebuildObstacleGrid();
  template <typename Fn>
  void forEachObstacleNear(glm::vec2 p, Fn&& fn) const {
    int cx = static_cast<int>((p.x + Terrain::SIZE * 0.5f) / kObstacleCell);
    int cz = static_cast<int>((p.y + Terrain::SIZE * 0.5f) / kObstacleCell);
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        int x = cx + dx, z = cz + dz;
        if (x < 0 || z < 0 || x >= kObstacleGridN || z >= kObstacleGridN) continue;
        for (int idx : obstacleGrid_[z * kObstacleGridN + x]) fn(props[idx]);
      }
  }

  // Rain food from the sky at p, in the acting god's name. Fails outside
  // that god's influence or with insufficient mana (a charged Miracle
  // Dispenser in an owned village covers the cost first). Deterministic.
  bool castFoodMiracle(const glm::vec3& p, int god = 0);

  // --- scaffold verbs (called by the hand; driven directly by tests) ---

  // Merge the held scaffold into a nearby one (sum capped at 7).
  // Returns the target index, or -1 if nothing merged.
  int tryCombineScaffold(int scaffoldIdx);

  // Commit a scaffold at its current position: validity-check the ground and
  // create the construction site for its stack count. `civicChoice` picks the
  // building for 3-stacks; `god` must own the hosting village. On success the
  // prop is consumed.
  bool tryPlaceScaffold(int scaffoldIdx, BuildingType civicChoice, int god = 0);

  // Would tryPlaceScaffold succeed here? (drives the ghost preview color)
  bool scaffoldPlacementValid(const glm::vec3& pos, int count, int god = 0) const;

  std::uint32_t seed() const { return seed_; }

 private:
  void foundTemple(int god, int villageIdx);
  void scatterProps();
  std::vector<glm::vec2> findVillageSites(int count) const;
  int scaffoldHostVillage(const glm::vec3& pos, int count, int god) const;

  static constexpr float kObstacleCell = 8.0f;
  static constexpr int kObstacleGridN =
      static_cast<int>(Terrain::SIZE / kObstacleCell) + 1;
  std::vector<std::vector<int>> obstacleGrid_;

  std::uint32_t seed_ = 1;
  std::uint32_t miracleCounter_ = 0;
  std::uint32_t editStroke_ = 0;  // seeds the paint brushes' determinism
};
