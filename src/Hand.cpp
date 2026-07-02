#include "Hand.h"

#include <algorithm>
#include <cmath>

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
  if (mode == Mode::Carry && heldProp >= 0) {
    hoverProp = -1;
    Prop& p = world.props[heldProp];

    glm::vec3 carryTarget = groundPoint + glm::vec3(0, 2.2f + p.radius, 0);
    if (!hasPrevHeldPos_) {
      prevHeldPos_ = p.pos;
      hasPrevHeldPos_ = true;
    }
    p.pos += (carryTarget - p.pos) * std::min(1.0f, 14.0f * dt);

    // Track the smoothed velocity the player is imparting for the throw.
    if (dt > 0.0001f) {
      glm::vec3 instant = (p.pos - prevHeldPos_) / dt;
      throwVel_ = glm::mix(throwVel_, instant, std::min(1.0f, 10.0f * dt));
    }
    prevHeldPos_ = p.pos;

    targetPos = p.pos + glm::vec3(0, p.radius + 1.1f, 0);
  } else {
    hoverProp = world.pickProp(rayOrigin, rayDir, 900.0f);
    if (hoverProp >= 0) {
      const Prop& p = world.props[hoverProp];
      targetPos = p.pos + glm::vec3(0, p.radius * 0.6f + 0.7f, 0);
    } else {
      targetPos = groundPoint + glm::vec3(0, 0.35f, 0);
    }
  }

  pos += (targetPos - pos) * std::min(1.0f, 25.0f * dt);
}

bool Hand::tryGrab(World& world) {
  if (mode != Mode::Free || hoverProp < 0) return false;
  heldProp = hoverProp;
  hoverProp = -1;
  Prop& p = world.props[heldProp];
  p.held = true;
  p.asleep = false;
  p.uprighting = false;
  mode = Mode::Carry;
  throwVel_ = glm::vec3(0.0f);
  hasPrevHeldPos_ = false;
  return true;
}

void Hand::release(World& world) {
  if (mode != Mode::Carry || heldProp < 0) return;

  glm::vec3 v = throwVel_ * 1.15f;
  float speed = glm::length(v);
  const float kMaxThrowSpeed = 70.0f;
  if (speed > kMaxThrowSpeed) v *= kMaxThrowSpeed / speed;

  world.throwProp(heldProp, v);
  heldProp = -1;
  mode = Mode::Free;
  hasPrevHeldPos_ = false;
}
