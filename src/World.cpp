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
  return t == PropType::Tree || t == PropType::Log || t == PropType::Food;
}

}  // namespace

void World::generate(std::uint32_t seed) {
  seed_ = seed;
  terrain.generate(seed);
  village = Village{};
  village.plan(*this, seed);   // flattens the site before the mesh is built
  scatterProps();
  village.spawnVillagers(*this, seed);
  dayCycle = DayCycle{};
  handPos = glm::vec3(0.0f, 1.0e9f, 0.0f);
  handSpeed = 0.0f;
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
