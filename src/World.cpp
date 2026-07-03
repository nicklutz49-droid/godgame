#include "World.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Noise.h"
#include "Physics.h"
#include "Tuning.h"
#include "Villagers.h"

namespace {

constexpr float kGravity = 28.0f;

using noise::XorShift;

bool propFloats(PropType t) {
  return t == PropType::Tree || t == PropType::Log || t == PropType::Food ||
         t == PropType::Scaffold || t == PropType::Body;
}

}  // namespace

// Score the island for settlement sites; greedily pick the best `count` with
// a minimum separation. Deterministic. Always returns at least one (the
// terraform-hard fallback), possibly fewer than asked on hostile islands.
std::vector<glm::vec2> World::findVillageSites(int count) const {
  static const glm::vec2 kDirs8[8] = {
      {1.0f, 0.0f},   {0.70711f, 0.70711f},   {0.0f, 1.0f},   {-0.70711f, 0.70711f},
      {-1.0f, 0.0f},  {-0.70711f, -0.70711f}, {0.0f, -1.0f},  {0.70711f, -0.70711f}};
  auto flatnessAt = [&](float x, float z) {
    float sum = 0.0f;
    for (int j = -2; j <= 2; ++j)
      for (int i = -2; i <= 2; ++i)
        sum += terrain.normalAt(x + static_cast<float>(i) * 6.0f,
                                z + static_cast<float>(j) * 6.0f).y;
    return sum / 25.0f;
  };
  auto coastDist = [&](float x, float z) {
    for (float d = 10.0f; d <= 140.0f; d += 10.0f)
      for (const glm::vec2& dir : kDirs8)
        if (terrain.heightAt(x + dir.x * d, z + dir.y * d) < 0.0f) return d;
    return 999.0f;
  };

  struct Candidate {
    float score;
    glm::vec2 pos;
  };
  std::vector<Candidate> all;
  const float lim = Terrain::SIZE * 0.42f;
  for (float z = -lim; z <= lim; z += 8.0f) {
    for (float x = -lim; x <= lim; x += 8.0f) {
      float h = terrain.heightAt(x, z);
      if (h < 2.5f || h > 14.0f) continue;
      float flat = flatnessAt(x, z);
      if (flat < 0.88f) continue;
      float coast = coastDist(x, z);
      if (coast > 120.0f) continue;
      float forest = noise::fbm(x * 0.016f, z * 0.016f, 3, seed_ + 31u);
      float score = flat * 3.0f + forest * 1.2f + (1.0f - coast / 120.0f) -
                    std::abs(h - 5.0f) * 0.08f;
      all.push_back({score, {x, z}});
    }
  }
  // Stable ordering: by score, ties broken by scan order (already the case
  // since sort is stable only if we make it so - use index tie-break).
  std::stable_sort(all.begin(), all.end(),
                   [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

  std::vector<glm::vec2> picked;
  for (const Candidate& c : all) {
    if (static_cast<int>(picked.size()) >= count) break;
    bool clear = true;
    for (const glm::vec2& p : picked)
      if (glm::distance(p, c.pos) < tune::kVillageMinSeparation) clear = false;
    if (clear) picked.push_back(c.pos);
  }

  if (picked.empty()) {
    // Hostile island: take the least-bad cell; the caller terraforms hard.
    Candidate best{-1.0e9f, {0.0f, 0.0f}};
    for (float z = -lim; z <= lim; z += 8.0f)
      for (float x = -lim; x <= lim; x += 8.0f) {
        float h = terrain.heightAt(x, z);
        float score = (h > 0.5f ? 5.0f - std::abs(h - 6.0f) * 0.3f : h) +
                      flatnessAt(x, z) * 2.0f;
        if (score > best.score) best = {score, {x, z}};
      }
    picked.push_back(best.pos);
  }
  return picked;
}

void World::generate(std::uint32_t seed) {
  seed_ = seed;
  miracleCounter_ = 0;
  terrain.generate(seed);

  // Found the player's home village on the best site, neutrals on the rest.
  std::vector<glm::vec2> sites = findVillageSites(1 + tune::kNeutralVillages);
  villages.clear();
  villages.resize(sites.size());
  for (std::size_t v = 0; v < sites.size(); ++v) {
    villages[v].owner = v == 0 ? 0 : -1;
    bool hard = v == 0 && sites.size() == 1 &&
                terrain.heightAt(sites[0].x, sites[0].y) < 2.5f;
    villages[v].plan(*this, seed + static_cast<std::uint32_t>(v) * 7919u, sites[v],
                     hard);
  }

  temple = Temple{};
  foundTemple();               // also flattens; must precede prop scatter
  scatterProps();
  for (std::size_t v = 0; v < villages.size(); ++v)
    villages[v].spawnVillagers(*this, seed, static_cast<int>(v));
  dayCycle = DayCycle{};
  handPos = glm::vec3(0.0f, 1.0e9f, 0.0f);
  handSpeed = 0.0f;
  obstacleGrid_.assign(kObstacleGridN * kObstacleGridN, {});
}

void World::foundTemple() {
  if (villages.empty() || !home().founded) return;
  const Village& village = home();
  // Just outside the village on the first workable compass direction. The
  // field direction (index 2) is excluded so its terrace is never disturbed.
  static const glm::vec2 kDirs[7] = {
      {0.70711f, 0.70711f},   {0.70711f, -0.70711f}, {-0.70711f, 0.70711f},
      {-0.70711f, -0.70711f}, {1.0f, 0.0f},          {-1.0f, 0.0f},
      {0.0f, -1.0f}};
  glm::vec2 c(village.center.x, village.center.z);
  glm::vec2 best = c + kDirs[0] * 48.0f;
  bool placed = false;
  // Area-averaged flatness, not a single sample - a lone flat point on a
  // mountainside would carve an ugly notch when terraced.
  auto flatAvg = [&](glm::vec2 p) {
    float sum = 0.0f;
    for (int j = -2; j <= 2; ++j)
      for (int i = -2; i <= 2; ++i)
        sum += terrain.normalAt(p.x + static_cast<float>(i) * 5.0f,
                                p.y + static_cast<float>(j) * 5.0f).y;
    return sum / 25.0f;
  };
  for (float minFlat : {0.90f, 0.84f}) {
    for (float dist : {48.0f, 56.0f, 64.0f}) {
      for (const glm::vec2& dir : kDirs) {
        glm::vec2 p = c + dir * dist;
        if (terrain.heightAt(p.x, p.y) < 1.5f) continue;
        if (flatAvg(p) < minFlat) continue;
        best = p;
        placed = true;
        break;
      }
      if (placed) break;
    }
    if (placed) break;
  }
  // Paranoia fallback: terraform hard wherever the first candidate was.
  float targetH = std::clamp(terrain.heightAt(best.x, best.y), 2.5f, 14.0f);
  terrain.flattenDisc(best.x, best.y, 16.0f, targetH, placed ? 0.95f : 1.0f);
  temple.founded = true;
  temple.pos = glm::vec3(best.x, terrain.heightAt(best.x, best.y), best.y);
  temple.yaw = std::atan2(village.center.x - best.x, village.center.z - best.y);
  temple.mana = tune::kManaStart;
  temple.manaMax = tune::kManaMax;
}

bool World::insideInfluence(const glm::vec3& p) const {
  if (temple.founded &&
      glm::distance(glm::vec2(p.x, p.z), glm::vec2(temple.pos.x, temple.pos.z)) <
          tune::kTempleInfluence)
    return true;
  for (const Village& v : villages) {
    if (!v.founded || v.owner != 0) continue;  // neutral villages project nothing
    if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(v.center.x, v.center.z)) <
        v.influenceRadius())
      return true;
  }
  return false;
}

void World::notifyDivineEvent(const glm::vec3& where, float fear, float awe) {
  for (Village& v : villages) v.notifyDivineEvent(where, fear, awe);
}

void World::rebuildObstacleGrid() {
  for (auto& cell : obstacleGrid_) cell.clear();
  if (obstacleGrid_.empty())
    obstacleGrid_.assign(kObstacleGridN * kObstacleGridN, {});
  for (std::size_t i = 0; i < props.size(); ++i) {
    const Prop& p = props[i];
    if (!p.alive || p.held || p.carrier >= 0) continue;
    if (p.type != PropType::Tree && p.type != PropType::Rock &&
        p.type != PropType::Stump)
      continue;
    int cx = static_cast<int>((p.pos.x + Terrain::SIZE * 0.5f) / kObstacleCell);
    int cz = static_cast<int>((p.pos.z + Terrain::SIZE * 0.5f) / kObstacleCell);
    if (cx < 0 || cz < 0 || cx >= kObstacleGridN || cz >= kObstacleGridN) continue;
    obstacleGrid_[cz * kObstacleGridN + cx].push_back(static_cast<int>(i));
  }
}

bool World::castFoodMiracle(const glm::vec3& p) {
  if (!temple.founded || !insideInfluence(p)) return false;

  // A charged Miracle Dispenser within reach covers the cost first.
  Building* dispenser = nullptr;
  for (Village& v : villages) {
    if (v.owner != 0) continue;
    for (Building& b : v.buildings)
      if (b.type == BuildingType::Dispenser && b.stage == 3 && b.charges > 0 &&
          glm::distance(glm::vec2(b.pos.x, b.pos.z), glm::vec2(p.x, p.z)) <
              tune::kDispenserCastRadius)
        dispenser = &b;
  }

  if (dispenser) {
    --dispenser->charges;
  } else {
    if (temple.mana < tune::kFoodMiracleCost) return false;
    temple.mana -= tune::kFoodMiracleCost;
  }

  XorShift rng(seed_ ^ (++miracleCounter_ * 0x9E3779B9u));
  for (int k = 0; k < tune::kFoodMiracleBundles; ++k) {
    Prop food;
    food.type = PropType::Food;
    food.scale = 1.0f;
    food.radius = 0.45f;
    food.resource = static_cast<float>(tune::kFoodPerCatch);
    food.pos = p + glm::vec3(rng.range(-2.2f, 2.2f), 9.0f + 2.5f * static_cast<float>(k),
                             rng.range(-2.2f, 2.2f));
    food.vel = glm::vec3(rng.range(-0.8f, 0.8f), 0.0f, rng.range(-0.8f, 0.8f));
    food.baseYaw = rng.range(0.0f, 6.2831f);
    food.rot = glm::angleAxis(food.baseYaw, glm::vec3(0, 1, 0));
    food.asleep = false;
    spawnProp(food);
  }

  // Food from heaven is the most convincing argument there is - to whichever
  // village watches it fall.
  notifyDivineEvent(p, 0.05f, tune::kAweMiracle);
  return true;
}

float World::restHeight(const Prop& p) const {
  float ground = terrain.heightAt(p.pos.x, p.pos.z);
  switch (p.type) {
    case PropType::Tree:
      return ground + kTreeHalfHeight * p.scale - 0.15f;
    case PropType::Log:
      return ground + 0.22f * p.scale;
    case PropType::Food:
      return ground + 0.25f * p.scale;
    case PropType::Stump:
      return ground + 0.35f * p.scale;
    case PropType::Scaffold:
      return ground + 0.68f * p.scale;  // lattice cube sits on its base
    case PropType::Body:
      return ground + 0.20f * p.scale;  // lying flat
    default:
      return ground + p.radius * 0.55f;
  }
}

int World::spawnProp(const Prop& p) {
  for (std::size_t i = 0; i < props.size(); ++i) {
    if (!props[i].alive && !props[i].held) {
      props[i] = p;
      return static_cast<int>(i);
    }
  }
  props.push_back(p);
  return static_cast<int>(props.size() - 1);
}

void World::scatterProps() {
  props.clear();
  XorShift rng(seed_ * 747796405u + 2891336453u);

  // Trees cluster into forests driven by low-frequency noise; a coarse grid
  // hash keeps them from overlapping.
  std::unordered_set<std::int64_t> occupied;
  auto cellKey = [](float x, float z) {
    const float cell = 4.5f;
    auto cx = static_cast<std::int64_t>(std::floor(x / cell));
    auto cz = static_cast<std::int64_t>(std::floor(z / cell));
    return cx * 1000003 + cz;
  };

  int placedTrees = 0;
  for (int attempt = 0; attempt < 9000 && placedTrees < 320; ++attempt) {
    float x = rng.range(-0.46f, 0.46f) * Terrain::SIZE;
    float z = rng.range(-0.46f, 0.46f) * Terrain::SIZE;
    float h = terrain.heightAt(x, z);
    if (h < 2.2f || h > 30.0f) continue;
    if (terrain.normalAt(x, z).y < 0.82f) continue;
    bool inVillage = false;
    for (const Village& v : villages) inVillage |= v.insideFootprint(x, z);
    if (inVillage) continue;
    if (temple.founded && glm::distance(glm::vec2(x, z),
                                        glm::vec2(temple.pos.x, temple.pos.z)) < 18.0f)
      continue;
    float forest = noise::fbm(x * 0.016f, z * 0.016f, 3, seed_ + 31u);
    if (forest < 0.52f && !(forest > 0.40f && rng.uniform() < 0.15f)) continue;
    if (occupied.count(cellKey(x, z))) continue;
    occupied.insert(cellKey(x, z));

    Prop p;
    p.type = PropType::Tree;
    p.variant = static_cast<int>(rng.next() % 3u);
    p.scale = rng.range(0.8f, 1.35f);
    p.radius = 1.6f * p.scale;
    p.baseYaw = rng.range(0.0f, 6.2831f);
    p.resource = static_cast<float>(tune::kChopSwings);
    p.pos = glm::vec3(x, 0.0f, z);
    p.pos.y = restHeight(p);
    p.rot = glm::angleAxis(p.baseYaw, glm::vec3(0, 1, 0));
    props.push_back(p);
    ++placedTrees;
  }

  int placedRocks = 0;
  for (int attempt = 0; attempt < 3000 && placedRocks < 90; ++attempt) {
    float x = rng.range(-0.48f, 0.48f) * Terrain::SIZE;
    float z = rng.range(-0.48f, 0.48f) * Terrain::SIZE;
    float h = terrain.heightAt(x, z);
    if (h < -3.0f) continue;  // allow a few in the shallows
    bool inVillage = false;
    for (const Village& v : villages) inVillage |= v.insideFootprint(x, z);
    if (inVillage) continue;
    if (temple.founded && glm::distance(glm::vec2(x, z),
                                        glm::vec2(temple.pos.x, temple.pos.z)) < 18.0f)
      continue;

    Prop p;
    p.type = PropType::Rock;
    p.variant = static_cast<int>(rng.next() % 3u);
    p.scale = rng.range(0.55f, 2.0f);
    p.radius = 0.9f * p.scale;
    p.baseYaw = rng.range(0.0f, 6.2831f);
    p.pos = glm::vec3(x, 0.0f, z);
    p.pos.y = restHeight(p);
    p.rot = glm::angleAxis(p.baseYaw, glm::vec3(0, 1, 0));
    props.push_back(p);
    ++placedRocks;
  }
}

void World::update(float dt) {
  dayCycle.advance(dt);
  rebuildObstacleGrid();
  villagersUpdate(*this, dt);

  for (std::size_t idx = 0; idx < props.size(); ++idx) {
    Prop& p = props[idx];
    if (p.alive) p.age += dt;  // bodies rot; everything else doesn't mind
    if (!p.alive || p.held || p.carrier >= 0) continue;

    // Fallen trees slowly right themselves and replant (never felled ones).
    if (p.uprighting) {
      glm::quat target = glm::angleAxis(p.baseYaw, glm::vec3(0, 1, 0));
      p.rot = glm::slerp(p.rot, target, std::min(1.0f, dt * 2.5f));
      float restY = restHeight(p);
      p.pos.y = glm::mix(p.pos.y, restY, std::min(1.0f, dt * 2.5f));
      if (std::abs(glm::dot(p.rot, target)) > 0.9995f) {
        p.rot = target;
        p.pos.y = restY;
        p.uprighting = false;
      }
      if (p.asleep) continue;
    }
    if (p.asleep) continue;

    p.vel.y -= kGravity * dt;

    // Water interaction: wood and food float, rocks sink with heavy drag.
    bool inWater = p.pos.y - p.radius * 0.5f < Terrain::WATER_LEVEL;
    if (inWater) {
      if (propFloats(p.type)) {
        float targetY = Terrain::WATER_LEVEL + p.radius * 0.3f;
        p.vel.y += (targetY - p.pos.y) * 8.0f * dt - p.vel.y * 3.0f * dt;
        p.vel.x *= 1.0f / (1.0f + 1.5f * dt);
        p.vel.z *= 1.0f / (1.0f + 1.5f * dt);
      } else {
        p.vel *= 1.0f / (1.0f + 2.5f * dt);
        p.vel.y -= kGravity * 0.2f * dt;  // net: sinks slowly
      }
    }

    p.pos += p.vel * dt;

    integrateTumble(p.rot, p.angVel, dt);

    // Terrain collision (single sphere) - shared with thrown villagers.
    float restitution = p.type == PropType::Rock ? 0.32f : 0.10f;
    bool onGround = false;
    collideSphereTerrain(p.pos, p.vel, p.angVel, p.radius, 0.55f, terrain,
                         restitution, 0.72f, 1.2f, onGround);

    // Fall asleep once settled (on land or afloat).
    bool settled = glm::length(p.vel) < 0.45f && glm::length(p.angVel) < 0.5f &&
                   (onGround || (inWater && propFloats(p.type)));
    if (settled) {
      p.restTimer += dt;
      if (p.restTimer > 0.4f) {
        p.asleep = true;
        p.vel = glm::vec3(0.0f);
        p.angVel = glm::vec3(0.0f);
        p.restTimer = 0.0f;

        // Resources coming to rest on a storage pad are absorbed - covers
        // villager hauling, gentle placement, and skill-shot throws alike.
        bool absorbed = false;
        if (p.type == PropType::Log || p.type == PropType::Food ||
            p.type == PropType::Tree) {
          for (Village& v : villages) {
            if (!v.founded || !v.inStorageRadius(p.pos)) continue;
            v.absorbProp(*this, static_cast<int>(idx));
            absorbed = true;
            break;
          }
        }
        if (absorbed) continue;

        if (p.type == PropType::Tree && onGround) {
          if (p.felled) {
            convertFelledTree(*this, static_cast<int>(idx));
          } else {
            p.uprighting = true;
          }
        }
      }
    } else {
      p.restTimer = 0.0f;
    }
  }

  for (Village& v : villages)
    if (v.founded) v.step(*this, dt);
}

int World::tryCombineScaffold(int scaffoldIdx) {
  if (scaffoldIdx < 0 || scaffoldIdx >= static_cast<int>(props.size())) return -1;
  Prop& held = props[scaffoldIdx];
  if (!held.alive || held.type != PropType::Scaffold) return -1;

  int best = -1;
  float bestD = tune::kScaffoldCombineRadius;
  for (std::size_t i = 0; i < props.size(); ++i) {
    if (static_cast<int>(i) == scaffoldIdx) continue;
    const Prop& p = props[i];
    if (!p.alive || p.held || p.carrier >= 0 || p.type != PropType::Scaffold)
      continue;
    if (p.resource + held.resource > static_cast<float>(tune::kMaxScaffoldStack) + 0.5f)
      continue;
    float d = glm::distance(glm::vec2(p.pos.x, p.pos.z),
                            glm::vec2(held.pos.x, held.pos.z));
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(i);
    }
  }
  if (best < 0) return -1;

  Prop& target = props[best];
  target.resource += held.resource;
  target.scale = 1.0f;
  target.radius = 0.9f + 0.18f * target.resource;  // taller stack, fatter pick
  target.asleep = true;
  target.pos.y = restHeight(target);
  held.alive = false;
  held.held = false;
  return best;
}

// Which OWNED village would host a stack placed at `pos`? Runs every
// validity rule; returns -1 if the placement is invalid everywhere.
int World::scaffoldHostVillage(const glm::vec3& pos, int count) const {
  glm::vec2 p2(pos.x, pos.z);

  // Center upgrades happen AT a totem; everything else needs open ground.
  bool isCenter =
      Village::buildingForStack(count, BuildingType::Store) == BuildingType::Center;
  for (std::size_t vi = 0; vi < villages.size(); ++vi) {
    const Village& v = villages[vi];
    if (!v.founded || v.owner != 0) continue;
    glm::vec2 c2(v.center.x, v.center.z);
    if (isCenter) {
      if (glm::distance(p2, c2) < 8.0f && v.centerIdx >= 0 &&
          v.buildings[v.centerIdx].level < 3)
        return static_cast<int>(vi);
      continue;
    }
    if (glm::distance(p2, c2) > tune::kBuildPlacementRange) continue;

    float footprint = count >= 4 ? 9.0f : (count >= 3 ? 4.5f : 3.5f);
    if (terrain.heightAt(pos.x, pos.z) < 1.5f) return -1;
    float flat = 0.0f;
    for (int j = -1; j <= 1; ++j)
      for (int i = -1; i <= 1; ++i)
        flat += terrain.normalAt(pos.x + static_cast<float>(i) * footprint * 0.5f,
                                 pos.z + static_cast<float>(j) * footprint * 0.5f).y;
    if (flat / 9.0f < 0.85f) return -1;
    // Clear of every village's buildings and fields, the temple, and props.
    for (const Village& other : villages) {
      if (!other.founded) continue;
      for (const Building& b : other.buildings) {
        if (b.stage < 0) continue;
        if (glm::distance(glm::vec2(b.pos.x, b.pos.z), p2) < footprint * 0.5f + 4.0f)
          return -1;
      }
      if (other.insideAnyField(pos.x, pos.z, footprint * 0.5f + 1.0f)) return -1;
    }
    if (temple.founded &&
        glm::distance(glm::vec2(temple.pos.x, temple.pos.z), p2) < 14.0f)
      return -1;
    for (const Prop& p : props) {
      if (!p.alive) continue;
      if (p.type != PropType::Tree && p.type != PropType::Rock &&
          p.type != PropType::Stump)
        continue;
      if (glm::distance(glm::vec2(p.pos.x, p.pos.z), p2) < footprint * 0.5f + p.radius)
        return -1;
    }
    return static_cast<int>(vi);
  }
  return -1;
}

bool World::scaffoldPlacementValid(const glm::vec3& pos, int count) const {
  return scaffoldHostVillage(pos, count) >= 0;
}

bool World::tryPlaceScaffold(int scaffoldIdx, BuildingType civicChoice) {
  if (scaffoldIdx < 0 || scaffoldIdx >= static_cast<int>(props.size())) return false;
  Prop& s = props[scaffoldIdx];
  if (!s.alive || s.type != PropType::Scaffold) return false;
  int count = std::clamp(static_cast<int>(std::lround(s.resource)), 1,
                         tune::kMaxScaffoldStack);
  int host = scaffoldHostVillage(s.pos, count);
  if (host < 0) return false;
  Village& village = villages[host];

  BuildingType type = Village::buildingForStack(count, civicChoice);
  if (type == BuildingType::Center) {
    // Upgrade the totem in place.
    Building& c = village.buildings[village.centerIdx];
    c.level = std::min(3, c.level + 1);
    s.alive = false;
    s.held = false;
    return true;
  }

  Building b;
  b.type = type;
  b.pos = glm::vec3(s.pos.x, terrain.heightAt(s.pos.x, s.pos.z), s.pos.z);
  glm::vec2 toCenter = glm::vec2(village.center.x, village.center.z) -
                       glm::vec2(s.pos.x, s.pos.z);
  b.yaw = std::atan2(toCenter.x, toCenter.y);
  b.stage = 0;
  b.tier = count;      // scaffolds ARE the material: no wood hauling
  b.woodCost = 0;
  village.buildings.push_back(b);
  s.alive = false;
  s.held = false;
  return true;
}

int World::pickProp(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const {
  int best = -1;
  float bestT = maxDist;
  for (std::size_t i = 0; i < props.size(); ++i) {
    const Prop& p = props[i];
    if (!p.alive || p.held) continue;
    float r = p.radius * 1.3f;  // generous pick sphere
    glm::vec3 oc = origin - p.pos;
    float b = glm::dot(oc, dir);
    float c = glm::dot(oc, oc) - r * r;
    float disc = b * b - c;
    if (disc < 0.0f) continue;
    float t = -b - std::sqrt(disc);
    if (t > 0.0f && t < bestT) {
      bestT = t;
      best = static_cast<int>(i);
    }
  }
  return best;
}

void World::throwProp(int index, const glm::vec3& velocity) {
  if (index < 0 || index >= static_cast<int>(props.size())) return;
  Prop& p = props[index];
  p.held = false;
  p.asleep = false;
  p.uprighting = false;
  p.restTimer = 0.0f;
  p.vel = velocity;

  // A bit of tumble proportional to the throw makes flights read better.
  glm::vec3 spinAxis = glm::cross(glm::normalize(velocity + glm::vec3(0, 0.001f, 0)),
                                  glm::vec3(0, 1, 0));
  float speed = glm::length(velocity);
  p.angVel = spinAxis * std::min(speed * 0.15f, 6.0f);
}
