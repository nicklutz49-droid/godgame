#include "Hand.h"

#include <algorithm>
#include <cmath>

#include "Tuning.h"

namespace {

// Position and carry radius of whatever the hand holds.
glm::vec3& heldPos(World& world, const GrabTarget& g) {
  if (g.isVillager()) return world.village.villagers[g.index].pos;
  return world.props[g.index].pos;
}

float heldRadius(const World& world, const GrabTarget& g) {
  if (g.isVillager()) return 1.0f * world.village.villagers[g.index].scale;
  return world.props[g.index].radius;
}

}  // namespace

void Hand::update(float dt, const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                  World& world) {
  // Where does the cursor ray meet the world? Terrain first, then the water
  // plane (whichever is nearer), falling back to a point along the ray.
  glm::vec3 terrainHit;
  bool hitTerrain = world.terrain.raycast(rayOrigin, rayDir, terrainHit);

  bool hitWater = false;
  glm::vec3 waterHit{0.0f};
  if (rayDir.y < -0.0001f) {
    float t = (Terrain::WATER_LEVEL - rayOrigin.y) / rayDir.y;
    if (t > 0.0f) {
      waterHit = rayOrigin + rayDir * t;
      hitWater = true;
    }
  }

  hasGround = hitTerrain || hitWater;
  if (hitTerrain && hitWater) {
    groundPoint = glm::distance(rayOrigin, terrainHit) <= glm::distance(rayOrigin, waterHit)
                      ? terrainHit
                      : waterHit;
  } else if (hitTerrain) {
    groundPoint = terrainHit;
  } else if (hitWater) {
    groundPoint = waterHit;
  } else {
    groundPoint = rayOrigin + rayDir * 60.0f;
  }

  glm::vec3 targetPos;
  if (mode == Mode::Carry && !held.none()) {
    hover.clear();
    glm::vec3& p = heldPos(world, held);
    float radius = heldRadius(world, held);

    glm::vec3 carryTarget = groundPoint + glm::vec3(0, 2.2f + radius, 0);
    if (!hasPrevHeldPos_) {
      prevHeldPos_ = p;
      hasPrevHeldPos_ = true;
    }
    p += (carryTarget - p) * std::min(1.0f, 14.0f * dt);

    // Track the smoothed velocity the player is imparting for the throw.
    if (dt > 0.0001f) {
      glm::vec3 instant = (p - prevHeldPos_) / dt;
      throwVel_ = glm::mix(throwVel_, instant, std::min(1.0f, 10.0f * dt));
    }
    prevHeldPos_ = p;

    targetPos = p + glm::vec3(0, radius + 1.1f, 0);
  } else {
    hover = pickTarget(world, rayOrigin, rayDir, 900.0f);
    if (hover.isProp()) {
      const Prop& p = world.props[hover.index];
      targetPos = p.pos + glm::vec3(0, p.radius * 0.6f + 0.7f, 0);
    } else if (hover.isVillager()) {
      const Villager& v = world.village.villagers[hover.index];
      targetPos = v.pos + glm::vec3(0, 2.0f * v.scale + 0.6f, 0);
    } else {
      targetPos = groundPoint + glm::vec3(0, 0.35f, 0);
    }
  }

  pos += (targetPos - pos) * std::min(1.0f, 25.0f * dt);
}

bool Hand::tryGrab(World& world) {
  if (mode != Mode::Free || hover.none()) return false;
  held = hover;
  hover.clear();
  if (held.isProp()) {
    Prop& p = world.props[held.index];
    p.held = true;
    p.asleep = false;
    p.uprighting = false;
    p.carrier = -1;  // snatched out of a villager's arms, possibly
    p.claimedBy = -1;
  } else {
    villagerGrabbed(world, held.index);
  }
  mode = Mode::Carry;
  throwVel_ = glm::vec3(0.0f);
  hasPrevHeldPos_ = false;
  return true;
}

void Hand::release(World& world) {
  if (mode != Mode::Carry || held.none()) return;

  glm::vec3 v = throwVel_ * 1.15f;
  float speed = glm::length(v);
  lastReleaseSpeed = speed;
  const bool gentle = speed < tune::kPlaceSpeed;

  if (held.isVillager()) {
    Villager& vg = world.village.villagers[held.index];
    if (gentle) {
      // Set them down on their feet just above the ground.
      float ground = world.terrain.heightAt(vg.pos.x, vg.pos.z);
      vg.pos.y = std::max(ground, Terrain::WATER_LEVEL - 1.0f) + 0.5f;
      villagerReleased(world, held.index, v * 0.4f, true);
    } else {
      villagerReleased(world, held.index, v, false);
    }
  } else {
    const float kMaxThrowSpeed = 70.0f;
    if (speed > kMaxThrowSpeed) v *= kMaxThrowSpeed / speed;
    int idx = held.index;
    world.throwProp(idx, v);
    // Gentle placement over the storage pad deposits resources immediately.
    Prop& p = world.props[idx];
    if (gentle && world.village.founded && world.village.inStorageRadius(p.pos) &&
        (p.type == PropType::Log || p.type == PropType::Food ||
         p.type == PropType::Tree)) {
      world.village.absorbProp(world, idx);
    }
  }

  held.clear();
  mode = Mode::Free;
  hasPrevHeldPos_ = false;
}
