#include "Hand.h"

#include <algorithm>
#include <cmath>

#include "Tuning.h"

namespace {

// Position and carry radius of whatever the hand holds.
glm::vec3& heldPos(World& world, const GrabTarget& g) {
  if (g.isVillager()) return world.villages[g.village].villagers[g.index].pos;
  return world.props[g.index].pos;
}

float heldRadius(const World& world, const GrabTarget& g) {
  if (g.isVillager())
    return 1.0f * world.villages[g.village].villagers[g.index].scale;
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
    // The hand can only act inside the god's influence: outside the rings it
    // can look, but nothing highlights and nothing can be grabbed.
    if (!hover.none()) {
      const glm::vec3& tp =
          hover.isVillager()
              ? world.villages[hover.village].villagers[hover.index].pos
              : world.props[hover.index].pos;
      if (!world.insideInfluence(tp)) hover.clear();
    }
    if (hover.isProp()) {
      const Prop& p = world.props[hover.index];
      targetPos = p.pos + glm::vec3(0, p.radius * 0.6f + 0.7f, 0);
    } else if (hover.isVillager()) {
      const Villager& v = world.villages[hover.village].villagers[hover.index];
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
    villagerGrabbed(world, held.village, held.index);
  }
  mode = Mode::Carry;
  throwVel_ = glm::vec3(0.0f);
  hasPrevHeldPos_ = false;
  return true;
}

int Hand::heldScaffoldCount(const World& world) const {
  if (mode != Mode::Carry || !held.isProp()) return 0;
  const Prop& p = world.props[held.index];
  if (!p.alive || p.type != PropType::Scaffold) return 0;
  return std::clamp(static_cast<int>(std::lround(p.resource)), 1,
                    tune::kMaxScaffoldStack);
}

void Hand::release(World& world) {
  if (mode != Mode::Carry || held.none()) return;

  glm::vec3 v = throwVel_ * 1.15f;
  float speed = glm::length(v);
  lastReleaseSpeed = speed;
  const bool gentle = speed < tune::kPlaceSpeed;

  // Scaffolds: a gentle release merges into a nearby scaffold, or commits a
  // construction site on valid ground; otherwise it just drops/throws.
  if (held.isProp() && world.props[held.index].type == PropType::Scaffold &&
      gentle) {
    int idx = held.index;
    Prop& s = world.props[idx];
    s.held = false;
    if (world.tryCombineScaffold(idx) >= 0 ||
        world.tryPlaceScaffold(idx, civicChoice)) {
      held.clear();
      mode = Mode::Free;
      hasPrevHeldPos_ = false;
      return;
    }
    s.held = true;  // neither applied; fall through to the normal drop
  }

  if (held.isVillager()) {
    Villager& vg = world.villages[held.village].villagers[held.index];
    if (gentle) {
      // Set them down on their feet just above the ground.
      float ground = world.terrain.heightAt(vg.pos.x, vg.pos.z);
      vg.pos.y = std::max(ground, Terrain::WATER_LEVEL - 1.0f) + 0.5f;
      villagerReleased(world, held.village, held.index, v * 0.4f, true);
    } else {
      villagerReleased(world, held.village, held.index, v, false);
    }
  } else {
    const float kMaxThrowSpeed = 70.0f;
    if (speed > kMaxThrowSpeed) v *= kMaxThrowSpeed / speed;
    int idx = held.index;
    world.throwProp(idx, v);
    // Gentle placement over a storage pad deposits resources immediately -
    // a gift from the god, and that village believes a little more for it.
    Prop& p = world.props[idx];
    if (gentle && (p.type == PropType::Log || p.type == PropType::Food ||
                   p.type == PropType::Tree)) {
      for (Village& vlg : world.villages) {
        if (!vlg.founded || !vlg.inStorageRadius(p.pos)) continue;
        vlg.absorbProp(world, idx);
        world.notifyDivineEvent(p.pos, 0.0f, tune::kAweGift);
        break;
      }
    }
    // The god personally laying a body to rest at a graveyard buries it.
    if (gentle && p.type == PropType::Body) {
      for (Village& vlg : world.villages)
        if (vlg.founded && vlg.buryBody(world, idx)) break;
    }
  }

  held.clear();
  mode = Mode::Free;
  hasPrevHeldPos_ = false;
}
