#include "GodAI.h"

#include <algorithm>
#include <cmath>

#include "Noise.h"
#include "Tuning.h"
#include "World.h"

using noise::XorShift;

namespace {

constexpr float kGravity = 28.0f;  // matches World/Villagers ballistics

glm::vec2 xz(const glm::vec3& p) { return {p.x, p.z}; }

const glm::vec2 kDirs8[8] = {
    {1.0f, 0.0f},   {0.70711f, 0.70711f},   {0.0f, 1.0f},
    {-0.70711f, 0.70711f}, {-1.0f, 0.0f},   {-0.70711f, -0.70711f},
    {0.0f, -1.0f},  {0.70711f, -0.70711f}};

// A 45-degree ballistic solution toward `to`, clamped to the hand's limit -
// the same physics a player throw obeys.
glm::vec3 aimThrow(const glm::vec3& from, const glm::vec3& to) {
  glm::vec2 flat = xz(to) - xz(from);
  float R = glm::length(flat);
  float dy = to.y - from.y;
  float v2 = R > dy + 1.0f ? kGravity * R * R / (R - dy)
                           : tune::kMaxThrowSpeed * tune::kMaxThrowSpeed;
  float v = std::min(std::sqrt(v2), tune::kMaxThrowSpeed);
  glm::vec2 dir = R > 0.01f ? flat / R : glm::vec2(1.0f, 0.0f);
  float comp = v * 0.70711f;
  return glm::vec3(dir.x * comp, comp, dir.y * comp);
}

// First spot on a deterministic ring scan where this stack may be committed.
glm::vec3 findPlacementSpot(const World& world, const Village& v, int count,
                            int god) {
  for (float r : {14.0f, 20.0f, 26.0f, 32.0f, 38.0f, 44.0f, 50.0f}) {
    for (const glm::vec2& dir : kDirs8) {
      glm::vec2 p = xz(v.center) + dir * r;
      glm::vec3 pos(p.x, world.terrain.heightAt(p.x, p.y), p.y);
      if (world.scaffoldPlacementValid(pos, count, god)) return pos;
    }
  }
  return glm::vec3(0.0f, -1.0e9f, 0.0f);
}

}  // namespace

void GodAI::reset(const World& world, int godIdx) {
  int keepProfile = profile;  // difficulty is a session choice, not AI state
  *this = GodAI{};
  profile = keepProfile;
  god = godIdx;
  rng_ = world.seed() * 0x9E3779B9u +
         static_cast<std::uint32_t>(godIdx + 1) * 2654435761u;
  handPos = restPoint(world);
  // Stagger decisions so two AI gods never think on the same frame.
  thinkTimer_ = tune::kAiProfiles[profile].thinkPeriod *
                (0.4f + 0.3f * static_cast<float>(godIdx));
}

glm::vec3 GodAI::restPoint(const World& world) const {
  const Temple& t = world.gods[god].temple;
  if (!t.founded) return glm::vec3(0.0f, 1.0e9f, 0.0f);
  return t.pos + glm::vec3(0.0f, tune::kAiHandHover + 1.5f, 0.0f);
}

void GodAI::moveToward(World& world, const glm::vec3& dest, float dt) {
  if (dest.y > 1.0e8f) {
    handVel = glm::vec3(0.0f);
    return;
  }
  if (handPos.y > 1.0e8f) handPos = dest;  // first placement, no flight
  glm::vec2 to = xz(dest) - xz(handPos);
  float d = glm::length(to);
  float step = tune::kAiProfiles[profile].handSpeed * dt;
  glm::vec2 next = d <= step ? xz(dest) : xz(handPos) + to * (step / d);
  float ground = std::max(world.terrain.heightAt(next.x, next.y),
                          Terrain::WATER_LEVEL);
  float targetY = ground + tune::kAiHandHover;
  glm::vec3 prev = handPos;
  handPos = glm::vec3(next.x,
                      handPos.y + (targetY - handPos.y) * std::min(1.0f, 6.0f * dt),
                      next.y);
  if (dt > 0.0001f)
    handVel = glm::mix(handVel, (handPos - prev) / dt, std::min(1.0f, 10.0f * dt));
}

bool GodAI::arrived(const glm::vec3& dest) const {
  return glm::distance(xz(handPos), xz(dest)) < 1.6f;
}

void GodAI::carryHeld(World& world, float dt) {
  if (held.none()) return;
  glm::vec3* p;
  float radius;
  if (held.isVillager()) {
    Villager& v = world.villages[held.village].villagers[held.index];
    p = &v.pos;
    radius = 1.0f * v.scale;
  } else {
    Prop& pr = world.props[held.index];
    p = &pr.pos;
    radius = pr.radius;
  }
  glm::vec3 want = handPos - glm::vec3(0.0f, radius + 1.1f, 0.0f);
  *p += (want - *p) * std::min(1.0f, 14.0f * dt);
}

bool GodAI::heldStillValid(const World& world) const {
  if (held.isVillager()) {
    if (held.village < 0 ||
        held.village >= static_cast<int>(world.villages.size()))
      return false;
    const Village& v = world.villages[held.village];
    if (held.index < 0 || held.index >= static_cast<int>(v.villagers.size()))
      return false;
    return v.villagers[held.index].alive;
  }
  if (held.isProp()) {
    if (held.index < 0 || held.index >= static_cast<int>(world.props.size()))
      return false;
    return world.props[held.index].alive;
  }
  return false;
}

void GodAI::executeGrab(World& world) {
  if (held.isProp()) {
    Prop& p = world.props[held.index];
    p.held = true;
    p.asleep = false;
    p.uprighting = false;
    p.carrier = -1;  // snatched out of a villager's arms, possibly
    p.claimedBy = -1;
  } else if (held.isVillager()) {
    villagerGrabbed(world, held.village, held.index, god);
  }
}

void GodAI::executeRelease(World& world) {
  switch (verb) {
    case Verb::Devote: {
      if (held.isVillager()) {
        Villager& v = world.villages[held.village].villagers[held.index];
        v.pos.x = target_.x;
        v.pos.z = target_.z;
        float ground = world.terrain.heightAt(v.pos.x, v.pos.z);
        v.pos.y = std::max(ground, Terrain::WATER_LEVEL - 1.0f) + 0.5f;
        villagerReleased(world, held.village, held.index, glm::vec3(0.0f), true,
                         god);
        ++devotions;
      }
      break;
    }
    case Verb::Build: {
      Prop& s = world.props[held.index];
      s.held = false;
      s.pos.x = target_.x;
      s.pos.z = target_.z;
      s.pos.y = world.restHeight(s);
      if (world.tryPlaceScaffold(held.index, civic_, god)) {
        ++placements;
      } else {
        s.asleep = false;  // invalid after all: let it drop and replan
      }
      break;
    }
    case Verb::Combine: {
      Prop& s = world.props[held.index];
      s.held = false;
      if (mergeInto_ >= 0 && mergeInto_ < static_cast<int>(world.props.size())) {
        const Prop& base = world.props[mergeInto_];
        if (base.alive && !base.held && base.type == PropType::Scaffold) {
          s.pos.x = base.pos.x + 0.6f;
          s.pos.z = base.pos.z;
          s.pos.y = world.restHeight(s);
        }
      }
      if (world.tryCombineScaffold(held.index) >= 0)
        ++combines;
      else
        s.asleep = false;
      break;
    }
    case Verb::Gift: {
      Prop& p = world.props[held.index];
      p.thrownByGod = god;  // the receiving village credits the sender
      if (throwIt_) {
        world.throwProp(held.index, aimThrow(p.pos, target_));
        ++gifts;
      } else {
        p.held = false;
        p.pos.x = target_.x;
        p.pos.z = target_.z;
        p.pos.y = world.terrain.heightAt(target_.x, target_.z) + 1.2f;
        p.vel = glm::vec3(0.0f);
        p.asleep = false;
        for (Village& vlg : world.villages) {
          if (!vlg.founded || !vlg.inStorageRadius(p.pos)) continue;
          vlg.absorbProp(world, held.index);
          if (!p.alive)
            world.notifyDivineEvent(god, p.pos, 0.0f, tune::kAweGift);
          break;
        }
        ++gifts;
      }
      break;
    }
    case Verb::Feed:
      if (world.castFoodMiracle(target_, god)) ++feeds;
      break;
    case Verb::Court:
      if (world.castFoodMiracle(target_, god)) ++courtCasts;
      break;
    case Verb::Rain:
      if (world.castMiracle(Miracle::Rain, target_, god)) ++rainCasts;
      break;
    case Verb::Smite:
      if (world.castMiracle(Miracle::Fireball, target_, god)) ++smites;
      break;
    case Verb::None:
      break;
  }
  held.clear();
  verb = Verb::None;
  phase = Phase::Rest;
  cooldown_ = tune::kAiProfiles[profile].actCooldown;
}

void GodAI::settle(World& world) {
  if (!held.none()) {
    abandon(world);
  } else {
    verb = Verb::None;
    phase = Phase::Rest;
  }
}

void GodAI::abandon(World& world) {
  if (held.isProp() && heldStillValid(world)) {
    Prop& p = world.props[held.index];
    p.held = false;
    p.asleep = false;
  } else if (held.isVillager() && heldStillValid(world)) {
    Villager& v = world.villages[held.village].villagers[held.index];
    float ground = world.terrain.heightAt(v.pos.x, v.pos.z);
    v.pos.y = std::max(ground, Terrain::WATER_LEVEL - 1.0f) + 0.5f;
    villagerReleased(world, held.village, held.index, glm::vec3(0.0f), true, god);
  }
  held.clear();
  verb = Verb::None;
  phase = Phase::Rest;
  cooldown_ = tune::kAiProfiles[profile].actCooldown;
}

void GodAI::update(World& world, float dt) {
  if (!world.gods[god].active) return;

  // A broken god's hand withdraws to its temple and goes still.
  if (world.godBroken(god)) {
    if (!held.none()) abandon(world);
    verb = Verb::None;
    phase = Phase::Rest;
    moveToward(world, restPoint(world), dt);
    return;
  }

  cooldown_ = std::max(0.0f, cooldown_ - dt);
  thinkTimer_ -= dt;

  switch (phase) {
    case Phase::Rest:
      moveToward(world, restPoint(world), dt);
      if (thinkTimer_ <= 0.0f) {
        thinkTimer_ = tune::kAiProfiles[profile].thinkPeriod;
        if (cooldown_ <= 0.0f) think(world);
      }
      break;

    case Phase::ToPickup: {
      // Validate the thing we are flying to (the player may have taken it).
      bool ok = false;
      if (held.isVillager()) {
        const Village& v = world.villages[held.village];
        const Villager& t = v.villagers[held.index];
        ok = t.alive && !t.held && !t.inside && t.state != VState::Airborne &&
             t.state != VState::Swim;
        if (ok) pickup_ = t.pos;  // villagers walk; track them
      } else if (held.isProp()) {
        const Prop& p = world.props[held.index];
        ok = p.alive && !p.held && p.carrier < 0;
        if (ok) pickup_ = p.pos;
      }
      if (!ok) {
        held.clear();
        verb = Verb::None;
        phase = Phase::Rest;
        cooldown_ = tune::kAiProfiles[profile].actCooldown;
        break;
      }
      moveToward(world, pickup_, dt);
      if (arrived(pickup_)) {
        executeGrab(world);
        phase = Phase::ToTarget;
      }
      break;
    }

    case Phase::ToTarget: {
      if (!held.none() && !heldStillValid(world)) {
        abandon(world);
        break;
      }
      moveToward(world, target_, dt);
      carryHeld(world, dt);
      bool close = verb == Verb::Gift && throwIt_
                       ? glm::distance(xz(handPos), xz(target_)) <
                             tune::kAiThrowRange
                       : arrived(target_);
      if (close) executeRelease(world);
      break;
    }
  }
}

// The priority ladder: feed a starving village, keep the worship engine
// manned, grow (scaffolds), then wage the war of faith (court neutrals,
// pressure the enemy). One order at a time; think() only runs at Rest.
void GodAI::think(World& world) {
  const God& me = world.gods[god];

  // --- Feed: an owned village running out of food gets a miracle. ---
  for (std::size_t vi = 0; vi < world.villages.size(); ++vi) {
    Village& v = world.villages[vi];
    if (!v.founded || v.owner != god) continue;
    if (v.food >= static_cast<int>(tune::kAiProfiles[profile].foodReserve)) continue;
    bool affords = me.mana >= tune::kFoodMiracleCost;
    if (!affords) {
      for (const Building& b : v.buildings)
        affords |= b.type == BuildingType::Dispenser && b.stage == 3 &&
                   b.charges > 0 &&
                   glm::distance(xz(b.pos), xz(v.storagePos())) <
                       tune::kDispenserCastRadius;
    }
    if (!affords) continue;
    verb = Verb::Feed;
    target_ = v.storagePos();
    targetVillage_ = static_cast<int>(vi);
    phase = Phase::ToTarget;
    return;
  }

  // --- Devote: keep enough dancers at every owned totem. ---
  for (std::size_t vi = 0; vi < world.villages.size(); ++vi) {
    Village& v = world.villages[vi];
    if (!v.founded || v.owner != god || v.centerIdx < 0) continue;
    int worshippers = 0, candidate = -1;
    for (std::size_t i = 0; i < v.villagers.size(); ++i) {
      const Villager& p = v.villagers[i];
      if (!p.alive || p.scale < 0.95f) continue;
      if (p.job == Job::Worshipper) {
        ++worshippers;
      } else if (candidate < 0 && p.job == Job::None && !p.held && !p.inside &&
                 p.state != VState::Airborne && p.state != VState::Swim) {
        candidate = static_cast<int>(i);
      }
    }
    int want = 1 + v.population() / tune::kAiWorshippersPer;
    if (worshippers >= want || candidate < 0) continue;
    verb = Verb::Devote;
    held.kind = GrabTarget::Kind::Villager;
    held.village = static_cast<int>(vi);
    held.index = candidate;
    pickup_ = v.villagers[candidate].pos;
    target_ = v.buildings[v.centerIdx].pos;
    targetVillage_ = static_cast<int>(vi);
    phase = Phase::ToPickup;
    return;
  }

  // --- Build: advance one growth project per owned village. ---
  for (std::size_t vi = 0; vi < world.villages.size(); ++vi) {
    Village& v = world.villages[vi];
    if (!v.founded || v.owner != god) continue;

    int underConstruction = 0;
    for (const Building& b : v.buildings)
      if (b.stage >= 0 && b.stage < 3) ++underConstruction;
    if (underConstruction >= 2) continue;  // one project at a time, roughly

    struct Stack {
      int idx;
      int count;
    };
    Stack stacks[12];
    int stackCount = 0;
    for (std::size_t i = 0; i < world.props.size() && stackCount < 12; ++i) {
      const Prop& p = world.props[i];
      if (!p.alive || p.held || p.carrier >= 0 || p.type != PropType::Scaffold)
        continue;
      if (glm::distance(xz(p.pos), xz(v.center)) >
          tune::kBuildPlacementRange + 12.0f)
        continue;
      stacks[stackCount++] = {static_cast<int>(i),
                              std::clamp(static_cast<int>(std::lround(p.resource)),
                                         1, tune::kMaxScaffoldStack)};
    }
    if (stackCount == 0) continue;

    // The governor's plan, from needs.
    int pop = v.population();
    int want = 0;
    BuildingType civic = BuildingType::Store;
    if (pop >= v.housingCapacity()) {
      want = 1;
    } else if (static_cast<int>(v.fields.size()) < 1 + pop / 10) {
      want = 4;
    } else if (v.food > static_cast<int>(0.85f * static_cast<float>(v.foodCap())) ||
               v.wood > static_cast<int>(0.85f * static_cast<float>(v.woodCap()))) {
      if (v.countCompleted(BuildingType::Store) < 2) want = 3;
    } else if (v.deaths > v.burials &&
               v.countCompleted(BuildingType::Graveyard) == 0) {
      want = 3;
      civic = BuildingType::Graveyard;
    } else if (v.countCompleted(BuildingType::Creche) == 0 && pop >= 12) {
      want = 3;
      civic = BuildingType::Creche;
    } else if (v.centerIdx >= 0 && v.buildings[v.centerIdx].level < 3 &&
               v.belief[god] > 0.45f) {
      want = 5;
    } else if (v.countCompleted(BuildingType::Dispenser) == 0 && pop >= 14) {
      want = 6;
    } else if (v.countCompleted(BuildingType::Wonder) == 0 && pop >= 18) {
      want = 7;
    }
    if (want == 0) continue;

    // A ready stack: place it.
    for (int s = 0; s < stackCount; ++s) {
      if (stacks[s].count != want) continue;
      glm::vec3 spot =
          want == 5 && v.centerIdx >= 0
              ? v.buildings[v.centerIdx].pos
              : findPlacementSpot(world, v, want, god);
      if (spot.y < -1.0e8f) break;  // nowhere to put it this think
      verb = Verb::Build;
      held.kind = GrabTarget::Kind::Prop;
      held.village = -1;
      held.index = stacks[s].idx;
      pickup_ = world.props[stacks[s].idx].pos;
      target_ = spot;
      targetVillage_ = static_cast<int>(vi);
      civic_ = civic;
      phase = Phase::ToPickup;
      return;
    }

    // Otherwise combine toward it: biggest base below the target count,
    // plus the biggest other stack that still fits.
    int base = -1, add = -1;
    for (int s = 0; s < stackCount; ++s) {
      if (stacks[s].count >= want) continue;
      if (base < 0 || stacks[s].count > stacks[base].count) base = s;
    }
    if (base >= 0) {
      for (int s = 0; s < stackCount; ++s) {
        if (s == base) continue;
        if (stacks[s].count + stacks[base].count > want) continue;
        if (add < 0 || stacks[s].count > stacks[add].count) add = s;
      }
    }
    if (base >= 0 && add >= 0) {
      verb = Verb::Combine;
      held.kind = GrabTarget::Kind::Prop;
      held.village = -1;
      held.index = stacks[add].idx;
      pickup_ = world.props[stacks[add].idx].pos;
      mergeInto_ = stacks[base].idx;
      target_ = world.props[stacks[base].idx].pos;
      targetVillage_ = static_cast<int>(vi);
      phase = Phase::ToPickup;
      return;
    }
  }

  // --- Rain: dry fields in an owned village get a shower (M11). The
  // dispenser gate binds gods equally; profiles differ only through the
  // mana reserve they keep. ---
  if (world.miracleUnlocked(Miracle::Rain, god) &&
      me.mana >= tune::kRainCost + tune::kAiProfiles[profile].manaReserve) {
    for (std::size_t vi = 0; vi < world.villages.size(); ++vi) {
      Village& v = world.villages[vi];
      if (!v.founded || v.owner != god || v.farmCells.empty()) continue;
      glm::vec3 fieldMid(0.0f);
      float growth = 0.0f;
      for (const FarmCell& c : v.farmCells) {
        fieldMid += glm::vec3(c.pos.x, 0.0f, c.pos.y);
        growth += c.growth;
      }
      float n = static_cast<float>(v.farmCells.size());
      fieldMid /= n;
      growth /= n;
      fieldMid.y = world.terrain.heightAt(fieldMid.x, fieldMid.z);
      if (growth > 0.45f) continue;                      // doing fine
      if (world.rainBoostAt(fieldMid) > 1.0f) continue;  // already raining
      verb = Verb::Rain;
      target_ = fieldMid;
      targetVillage_ = static_cast<int>(vi);
      phase = Phase::ToTarget;
      return;
    }
  }

  // --- Sustain: with the pool overflowing, spend it the way a player
  // would - a miracle over the owned village whose faith sags most (belief
  // is worship fuel and ring reach). ---
  if (me.mana >= 0.9f * me.manaMax) {
    int worst = -1;
    float worstB = 0.55f;
    for (std::size_t vi = 0; vi < world.villages.size(); ++vi) {
      const Village& v = world.villages[vi];
      if (!v.founded || v.owner != god || v.population() <= 0) continue;
      if (v.belief[god] < worstB) {
        worstB = v.belief[god];
        worst = static_cast<int>(vi);
      }
    }
    if (worst >= 0) {
      verb = Verb::Court;
      target_ = world.villages[worst].center;
      targetVillage_ = worst;
      phase = Phase::ToTarget;
      return;
    }
  }

  // --- Court: win over the nearest neutral village. ---
  int courtV = -1;
  float courtD = 1.0e9f;
  for (std::size_t nv = 0; nv < world.villages.size(); ++nv) {
    const Village& n = world.villages[nv];
    if (!n.founded || n.owner >= 0 || n.population() <= 0) continue;
    for (const Village& mine : world.villages) {
      if (!mine.founded || mine.owner != god) continue;
      float d = glm::distance(xz(n.center), xz(mine.center));
      if (d < courtD) {
        courtD = d;
        courtV = static_cast<int>(nv);
      }
    }
  }
  if (courtV >= 0) {
    const Village& n = world.villages[courtV];
    if (world.insideInfluence(n.center, god) &&
        me.mana >= tune::kFoodMiracleCost + tune::kAiProfiles[profile].manaReserve) {
      verb = Verb::Court;
      target_ = n.center;
      targetVillage_ = courtV;
      phase = Phase::ToTarget;
      return;
    }
    if (orderGiftRun(world, courtV)) return;
  }

  // --- Smite (CRUEL only, M11): a fireball on the enemy's weakest village,
  // once its own Wonder stands, the pool is deep, and the ring genuinely
  // reaches - influence limits the god just like it limits the player. ---
  if (profile == 2 && world.miracleUnlocked(Miracle::Fireball, god) &&
      me.mana >= tune::kFireballCost + tune::kAiProfiles[profile].manaReserve) {
    XorShift r(rng_);
    bool wrathful = r.uniform() < tune::kAiProfiles[profile].aggression * 0.5f;
    rng_ = r.state;
    if (wrathful) {
      int enemyV = -1;
      float weakest = 2.0f;
      for (std::size_t nv = 0; nv < world.villages.size(); ++nv) {
        const Village& n = world.villages[nv];
        if (!n.founded || n.owner < 0 || n.owner == god) continue;
        if (!world.insideInfluence(n.center, god)) continue;
        if (n.belief[n.owner] < weakest) {
          weakest = n.belief[n.owner];
          enemyV = static_cast<int>(nv);
        }
      }
      if (enemyV >= 0) {
        verb = Verb::Smite;
        target_ = world.villages[enemyV].center;
        targetVillage_ = enemyV;
        phase = Phase::ToTarget;
        return;
      }
    }
  }

  // --- Contest: no neutrals left (or mana burning a hole) - pressure the
  // enemy's weakest village with gifts so the steal ratchet has something
  // to work with. Aggression keeps this occasional, not obsessive. ---
  bool wealthy = me.mana > 0.85f * me.manaMax;
  if (courtV < 0 || wealthy) {
    XorShift r(rng_);
    bool bold = r.uniform() < tune::kAiProfiles[profile].aggression;
    rng_ = r.state;
    if (bold) {
      int enemyV = -1;
      float weakest = 2.0f;
      for (std::size_t nv = 0; nv < world.villages.size(); ++nv) {
        const Village& n = world.villages[nv];
        if (!n.founded || n.owner < 0 || n.owner == god) continue;
        if (n.belief[n.owner] < weakest) {
          weakest = n.belief[n.owner];
          enemyV = static_cast<int>(nv);
        }
      }
      if (enemyV >= 0 && orderGiftRun(world, enemyV)) return;
    }
  }
}

// A gift run: grab a loose food (or log) inside our influence and deliver it
// onto `villageIdx`'s pad - hurled from range so the hand can get back to
// work. Returns false when nothing suitable is lying around.
bool GodAI::orderGiftRun(World& world, int villageIdx) {
  const Village& n = world.villages[villageIdx];
  glm::vec3 pad = n.storagePos();
  int pick = -1;
  float bestD = 1.0e9f;
  bool bestFood = false;
  for (std::size_t i = 0; i < world.props.size(); ++i) {
    const Prop& p = world.props[i];
    if (!p.alive || p.held || p.carrier >= 0 || p.claimedBy >= 0) continue;
    if (p.type != PropType::Food && p.type != PropType::Log) continue;
    if (!world.insideInfluence(p.pos, god)) continue;
    bool food = p.type == PropType::Food;
    float d = glm::distance(xz(p.pos), xz(pad));
    // Prefer food over wood, then the shortest delivery.
    if (bestFood && !food) continue;
    if (food && !bestFood) bestD = 1.0e9f;
    if (d < bestD) {
      bestD = d;
      pick = static_cast<int>(i);
      bestFood = food;
    }
  }
  if (pick < 0) return false;
  verb = Verb::Gift;
  held.kind = GrabTarget::Kind::Prop;
  held.village = -1;
  held.index = pick;
  pickup_ = world.props[pick].pos;
  target_ = pad;
  targetVillage_ = villageIdx;
  throwIt_ = true;
  phase = Phase::ToPickup;
  return true;
}
