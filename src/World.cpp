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

void World::generate(std::uint32_t seed) {
  seed_ = seed;
  miracleCounter_ = 0;
  terrain.generate(seed);
  village = Village{};
  village.plan(*this, seed);   // flattens the site before the mesh is built
  temple = Temple{};
  foundTemple();               // also flattens; must precede prop scatter
  scatterProps();
  village.spawnVillagers(*this, seed);
  dayCycle = DayCycle{};
  handPos = glm::vec3(0.0f, 1.0e9f, 0.0f);
  handSpeed = 0.0f;
}

void World::foundTemple() {
  if (!village.founded) return;
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
  if (village.founded &&
      glm::distance(glm::vec2(p.x, p.z),
                    glm::vec2(village.center.x, village.center.z)) <
          village.influenceRadius())
    return true;
  return false;
}

bool World::castFoodMiracle(const glm::vec3& p) {
  if (!temple.founded || !insideInfluence(p)) return false;

  // A charged Miracle Dispenser within reach covers the cost first.
  Building* dispenser = nullptr;
  for (Building& b : village.buildings)
    if (b.type == BuildingType::Dispenser && b.stage == 3 && b.charges > 0 &&
        glm::distance(glm::vec2(b.pos.x, b.pos.z), glm::vec2(p.x, p.z)) <
            tune::kDispenserCastRadius)
      dispenser = &b;

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

  // Food from heaven is the most convincing argument there is.
  village.notifyDivineEvent(p, 0.05f, tune::kAweMiracle);
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
    if (village.insideFootprint(x, z)) continue;
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
    if (village.insideFootprint(x, z)) continue;
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
  if (village.founded) villagersUpdate(*this, dt);

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

        // Resources coming to rest on the storage pad are absorbed - covers
        // villager hauling, gentle placement, and skill-shot throws alike.
        if (village.founded && village.inStorageRadius(p.pos) &&
            (p.type == PropType::Log || p.type == PropType::Food ||
             p.type == PropType::Tree)) {
          village.absorbProp(*this, static_cast<int>(idx));
          continue;
        }

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

  if (village.founded) village.step(*this, dt);
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

bool World::scaffoldPlacementValid(const glm::vec3& pos, int count) const {
  if (!village.founded) return false;
  glm::vec2 p2(pos.x, pos.z);
  glm::vec2 c2(village.center.x, village.center.z);

  // Center upgrades happen AT the totem; everything else needs open ground.
  if (Village::buildingForStack(count, BuildingType::Store) == BuildingType::Center)
    return glm::distance(p2, c2) < 8.0f &&
           village.buildings[village.centerIdx].level < 3;

  if (glm::distance(p2, c2) > tune::kBuildPlacementRange) return false;
  float footprint = count >= 4 ? 9.0f : (count >= 3 ? 4.5f : 3.5f);
  if (terrain.heightAt(pos.x, pos.z) < 1.5f) return false;
  // Area flatness over the footprint.
  float flat = 0.0f;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i)
      flat += terrain.normalAt(pos.x + static_cast<float>(i) * footprint * 0.5f,
                               pos.z + static_cast<float>(j) * footprint * 0.5f).y;
  if (flat / 9.0f < 0.85f) return false;
  // Clear of buildings, fields, the temple, and blocking props.
  for (const Building& b : village.buildings) {
    if (b.stage < 0) continue;
    if (glm::distance(glm::vec2(b.pos.x, b.pos.z), p2) < footprint * 0.5f + 4.0f)
      return false;
  }
  if (village.insideAnyField(pos.x, pos.z, footprint * 0.5f + 1.0f)) return false;
  if (temple.founded &&
      glm::distance(glm::vec2(temple.pos.x, temple.pos.z), p2) < 14.0f)
    return false;
  for (const Prop& p : props) {
    if (!p.alive) continue;
    if (p.type != PropType::Tree && p.type != PropType::Rock &&
        p.type != PropType::Stump)
      continue;
    if (glm::distance(glm::vec2(p.pos.x, p.pos.z), p2) < footprint * 0.5f + p.radius)
      return false;
  }
  return true;
}

bool World::tryPlaceScaffold(int scaffoldIdx, BuildingType civicChoice) {
  if (scaffoldIdx < 0 || scaffoldIdx >= static_cast<int>(props.size())) return false;
  Prop& s = props[scaffoldIdx];
  if (!s.alive || s.type != PropType::Scaffold) return false;
  int count = std::clamp(static_cast<int>(std::lround(s.resource)), 1,
                         tune::kMaxScaffoldStack);
  if (!scaffoldPlacementValid(s.pos, count)) return false;

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
