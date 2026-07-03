#include "Villagers.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

#include "Noise.h"
#include "Physics.h"
#include "Tuning.h"
#include "Villager.h"
#include "World.h"

using noise::XorShift;

namespace {

constexpr float kGravity = 28.0f;
constexpr float kPi = 3.14159265f;

glm::vec2 xz(const glm::vec3& p) { return {p.x, p.z}; }

float wrapAngle(float a) {
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

float turnToward(float yaw, float target, float maxDelta) {
  return yaw + std::clamp(wrapAngle(target - yaw), -maxDelta, maxDelta);
}

bool isVoluntary(VState s) { return s <= VState::Sleep; }
bool isWalking(VState s) {
  return s == VState::Wander || s == VState::GoTo || s == VState::Haul ||
         s == VState::GoEat || s == VState::GoHome || s == VState::Panic;
}

glm::vec3 doorPos(const Building& b) {
  glm::vec3 f(std::sin(b.yaw), 0.0f, std::cos(b.yaw));
  return b.pos + f * 2.8f;
}

void releaseClaims(World& w, Village& vil, int vi, int i, Villager& v) {
  int myId = villagerId(vi, i);
  for (Prop& p : w.props)
    if (p.claimedBy == myId) p.claimedBy = -1;
  for (FarmCell& c : vil.farmCells)
    if (c.claimedBy == i) c.claimedBy = -1;
  v.targetProp = -1;
  v.targetCell = -1;
  v.targetBuilding = -1;
}

void dropCargo(World& w, Villager& v) {
  if (v.carriedProp < 0) return;
  Prop& p = w.props[v.carriedProp];
  if (p.carrier >= 0) {
    p.carrier = -1;
    p.asleep = false;
    p.vel = glm::vec3(0.0f);
  }
  v.carriedProp = -1;
}

void goEat(Village& vil, Villager& v) {
  v.state = VState::GoEat;
  v.moveTarget = xz(vil.storagePos());
}

enum class DeathCause { Impact, Drowned, Starved };

// THE death funnel: every villager death flows through here (impact landings,
// drowning, starvation). Never called while held - the divine grip preserves.
void villagerKill(World& w, Village& vil, int vi, int idx, DeathCause cause) {
  Villager& v = vil.villagers[idx];
  if (!v.alive) return;
  dropCargo(w, v);
  releaseClaims(w, vil, vi, idx, v);
  v.alive = false;
  v.held = false;
  v.inside = false;
  v.state = VState::Idle;
  vil.onVillagerDeath(idx);  // frees the bed
  ++vil.deaths;

  // The body: a real prop, carryable to the graveyard (it floats, grimly).
  Prop body;
  body.type = PropType::Body;
  body.variant = v.variant;
  body.scale = v.scale;
  body.radius = 0.6f * v.scale;
  body.pos = v.pos + glm::vec3(0.0f, 0.3f, 0.0f);
  body.vel = cause == DeathCause::Impact ? v.vel * 0.3f : glm::vec3(0.0f);
  body.baseYaw = v.yaw;
  body.rot = glm::angleAxis(v.yaw, glm::vec3(0, 1, 0));
  body.asleep = false;
  w.spawnProp(body);

  // Every death shakes the village's faith in its patron, and terrifies.
  if (vil.owner >= 0) {
    vil.belief[vil.owner] = std::max(
        tune::kBeliefFloor, vil.belief[vil.owner] - tune::kBeliefDeathPenalty);
  }
  w.notifyDivineEvent(-1, v.pos, 0.85f, 0.0f);
}

void goHome(Village& vil, Villager& v) {
  v.state = VState::GoHome;
  glm::vec3 t = v.home >= 0 ? doorPos(vil.buildings[v.home])
                            : vil.campfirePos();
  v.moveTarget = xz(t);
}

// Nearest prop matching a predicate, respecting claims and the blacklist.
template <typename Pred>
int nearestProp(const World& w, const Village& vil, const Villager& v, int myId,
                Pred pred) {
  int best = -1;
  float bestD = 1.0e9f;
  for (std::size_t i = 0; i < w.props.size(); ++i) {
    const Prop& p = w.props[i];
    if (!p.alive || p.held || p.carrier >= 0) continue;
    if (p.claimedBy != -1 && p.claimedBy != myId) continue;
    if (static_cast<int>(i) == v.blacklistProp) continue;
    if (!pred(p)) continue;
    if (glm::distance(xz(p.pos), xz(vil.center)) > tune::kWorkRadius) continue;
    float d = glm::distance(xz(p.pos), xz(v.pos));
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

int openBuildSite(const Village& vil) {
  for (std::size_t i = 0; i < vil.buildings.size(); ++i) {
    const Building& b = vil.buildings[i];
    if (b.stage >= 0 && b.stage < 3) return static_cast<int>(i);
  }
  return -1;
}

int completedWorkshop(const Village& vil) {
  for (std::size_t i = 0; i < vil.buildings.size(); ++i) {
    const Building& b = vil.buildings[i];
    if (b.type == BuildingType::Workshop && b.stage == 3)
      return static_cast<int>(i);
  }
  return -1;
}

int looseScaffoldCount(const World& w, const Village& vil) {
  int n = 0;
  for (const Prop& p : w.props) {
    if (!(p.alive && p.type == PropType::Scaffold && !p.held && p.carrier < 0))
      continue;
    if (glm::distance(xz(p.pos), xz(vil.center)) > tune::kWorkRadius) continue;
    ++n;
  }
  return n;
}

int spawnCarried(World& w, Villager& v, int myId, PropType type, float meals) {
  Prop p;
  p.type = type;
  p.scale = 1.0f;
  p.radius = type == PropType::Log ? 0.5f : 0.45f;
  p.resource = meals;
  p.pos = v.pos + glm::vec3(0.0f, 1.1f * v.scale, 0.0f);
  p.asleep = true;
  int idx = w.spawnProp(p);
  w.props[idx].carrier = myId;
  v.carriedProp = idx;
  return idx;
}

// --- job planning (called from the think tick when idle-ish) ---

void planForester(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  int myId = villagerId(vi, i);
  if (v.carriedProp >= 0) {  // resume hauling
    v.state = VState::Haul;
    v.moveTarget = xz(vil.storagePos());
    return;
  }
  // Loose logs outrank standing trees - free cleanup behavior.
  int log = nearestProp(w, vil, v, myId,
                        [](const Prop& p) { return p.type == PropType::Log; });
  if (log >= 0) {
    w.props[log].claimedBy = myId;
    v.targetProp = log;
    v.state = VState::GoTo;
    v.moveTarget = xz(w.props[log].pos);
    return;
  }
  int tree = nearestProp(w, vil, v, myId, [](const Prop& p) {
    return p.type == PropType::Tree && p.resource > 0.0f && !p.felled && p.asleep;
  });
  if (tree >= 0) {
    w.props[tree].claimedBy = myId;
    v.targetProp = tree;
    v.state = VState::GoTo;
    v.moveTarget = xz(w.props[tree].pos);
    return;
  }
  v.state = VState::Wander;  // visibly unemployed at the village edge
  v.moveTarget = xz(vil.center);
}

void planFarmer(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  int myId = villagerId(vi, i);
  if (v.carriedProp >= 0) {
    v.state = VState::Haul;
    v.moveTarget = xz(vil.storagePos());
    return;
  }
  // Loose food (dropped cargo, miracle bundles) gets gathered first.
  int loose = nearestProp(w, vil, v, myId,
                          [](const Prop& p) { return p.type == PropType::Food; });
  if (loose >= 0) {
    w.props[loose].claimedBy = myId;
    v.targetProp = loose;
    v.state = VState::GoTo;
    v.moveTarget = xz(w.props[loose].pos);
    return;
  }
  auto& cells = vil.farmCells;
  int ripe = -1, least = -1;
  float leastG = 0.96f;
  for (std::size_t c = 0; c < cells.size(); ++c) {
    if (cells[c].claimedBy != -1 && cells[c].claimedBy != i) continue;
    if (cells[c].growth >= 1.0f && ripe < 0) ripe = static_cast<int>(c);
    if (cells[c].growth < leastG && cells[c].tendedTimer <= 0.0f) {
      leastG = cells[c].growth;
      least = static_cast<int>(c);
    }
  }
  int cell = ripe >= 0 ? ripe : least;
  if (cell >= 0) {
    cells[cell].claimedBy = i;
    v.targetCell = cell;
    v.state = VState::GoTo;
    v.moveTarget = cells[cell].pos;
    return;
  }
  v.state = VState::Wander;
  v.moveTarget = vil.fields.empty() ? xz(vil.center)
                                          : vil.fields[0].center;
}

void planFisherman(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  (void)w;
  (void)vi;
  if (v.carriedProp >= 0) {
    v.state = VState::Haul;
    v.moveTarget = xz(vil.storagePos());
    return;
  }
  if (vil.fishingSpots.empty()) {
    v.state = VState::Wander;
    v.moveTarget = xz(vil.center);
    return;
  }
  int best = 0;
  float bestD = 1.0e9f;
  for (std::size_t s = 0; s < vil.fishingSpots.size(); ++s) {
    float d = glm::distance(xz(vil.fishingSpots[s]), xz(v.pos));
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(s);
    }
  }
  v.targetBuilding = -1;
  v.targetProp = -1;
  v.state = VState::GoTo;
  glm::vec3 spot = vil.fishingSpots[best];
  // Fishermen share spots; fan out a little by index so they don't stack.
  v.moveTarget = xz(spot) + glm::vec2(std::sin(static_cast<float>(i)),
                                      std::cos(static_cast<float>(i))) *
                                1.6f;
}

void planWorshipper(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  (void)w;
  (void)vi;
  if (vil.centerIdx < 0) {
    v.state = VState::Wander;
    v.moveTarget = xz(vil.center);
    return;
  }
  const Building& totem = vil.buildings[vil.centerIdx];
  // Join the dance ring wherever is closest to where they stand.
  glm::vec2 from = xz(v.pos) - xz(totem.pos);
  v.danceAngle = glm::length(from) > 0.5f ? std::atan2(from.x, from.y)
                                          : static_cast<float>(i) * 1.3f;
  v.state = VState::GoTo;
  v.moveTarget = xz(totem.pos) + glm::vec2(std::sin(v.danceAngle), std::cos(v.danceAngle)) *
                                     tune::kWorshipDanceRadius;
}

void planBuilder(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  (void)vi;
  int site = openBuildSite(vil);
  if (site < 0) {
    dropCargo(w, v);
    // No construction to serve: work the workshop bench, crafting scaffolds,
    // as long as there's wood and the yard isn't already full of them.
    int shop = completedWorkshop(vil);
    if (shop >= 0 && vil.wood >= tune::kScaffoldWoodCost &&
        looseScaffoldCount(w, vil) < tune::kMaxLooseScaffolds) {
      v.targetBuilding = shop;
      v.state = VState::GoTo;
      v.moveTarget = xz(vil.buildings[shop].pos);
      return;
    }
    v.state = VState::Wander;
    v.moveTarget = xz(vil.storagePos());
    return;
  }
  Building& b = vil.buildings[site];
  v.targetBuilding = site;
  if (v.carriedProp >= 0) {
    v.state = VState::GoTo;  // deliver the log to the site
    v.moveTarget = xz(b.pos);
  } else if (b.woodDelivered < b.woodCost) {
    if (vil.wood > 0) {
      v.state = VState::GoTo;  // withdraw at the storage pad
      v.moveTarget = xz(vil.storagePos());
    } else {
      v.state = VState::Wander;  // visibly waiting for wood
      v.moveTarget = xz(vil.storagePos());
    }
  } else {
    v.state = VState::GoTo;  // hammer
    v.moveTarget = xz(b.pos);
  }
}

void planJob(World& w, Village& vil, int vi, int i) {
  switch (vil.villagers[i].job) {
    case Job::Forester: planForester(w, vil, vi, i); break;
    case Job::Farmer: planFarmer(w, vil, vi, i); break;
    case Job::Fisherman: planFisherman(w, vil, vi, i); break;
    case Job::Builder: planBuilder(w, vil, vi, i); break;
    case Job::Worshipper: planWorshipper(w, vil, vi, i); break;
    default: break;
  }
}

// Re-check that the current task still makes sense (the hand may have stolen
// the tree, another builder may have finished the site...).
void validateJob(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  int myId = villagerId(vi, i);
  if (v.targetProp >= 0) {
    const Prop& p = w.props[v.targetProp];
    bool ok = p.alive && !p.held && p.carrier < 0 &&
              (p.claimedBy == myId || p.claimedBy == -1);
    if (ok && p.type == PropType::Tree)
      ok = p.resource > 0.0f && !p.felled && p.asleep;
    if (ok && p.type == PropType::Log) ok = true;
    if (!ok) {
      releaseClaims(w, vil, vi, i, v);
      v.state = VState::Idle;  // shrug; re-plan next think
      v.stateTimer = 0.6f;
      return;
    }
    if (v.state == VState::GoTo) v.moveTarget = xz(p.pos);  // it may have moved
  }
  if (v.targetBuilding >= 0) {
    const Building& b = vil.buildings[v.targetBuilding];
    // A completed Workshop is a valid destination - that's a crafting trip.
    bool crafting = b.type == BuildingType::Workshop && b.stage == 3;
    if (v.job == Job::Builder && !crafting && (b.stage < 0 || b.stage >= 3)) {
      releaseClaims(w, vil, vi, i, v);
      v.state = VState::Idle;
      v.stateTimer = 0.4f;
    }
  }
}

// --- arrivals & work cycles ---

void arriveAtTarget(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  (void)vi;
  switch (v.state) {
    case VState::Wander:
      v.state = VState::Idle;
      {
        XorShift r(v.rng);
        v.stateTimer = r.range(1.5f, 5.0f);
        v.rng = r.state;
      }
      break;
    case VState::GoEat:
      if (vil.food > 0) {
        v.state = VState::Eat;
        v.stateTimer = tune::kEatSeconds;
      } else {
        v.state = VState::Idle;  // pile is empty; retry after a moment
        v.stateTimer = 2.5f;
      }
      break;
    case VState::GoHome:
      if (v.home >= 0) {
        v.inside = true;  // slide into the house, hidden until dawn
        v.state = VState::Sleep;
      } else {
        v.state = VState::Sleep;  // curl up by the campfire
      }
      break;
    case VState::Haul:
      v.state = VState::Work;  // deposit (or dig, for a burial)
      v.workTimer = (v.carriedProp >= 0 &&
                     w.props[v.carriedProp].type == PropType::Body)
                        ? tune::kBurySeconds
                        : tune::kDepositSeconds;
      break;
    case VState::GoTo: {
      // What we start doing depends on what we walked to.
      if (v.targetProp >= 0) {
        const Prop& p = w.props[v.targetProp];
        v.yaw = std::atan2(p.pos.x - v.pos.x, p.pos.z - v.pos.z);
        v.state = VState::Work;
        v.workTimer = p.type == PropType::Tree ? tune::kChopSwingSeconds
                                               : tune::kPickupSeconds;
        v.workCount = 0;
      } else if (v.targetCell >= 0) {
        const FarmCell& c = vil.farmCells[v.targetCell];
        v.state = VState::Work;
        v.workTimer = c.growth >= 1.0f ? tune::kHarvestSeconds : tune::kTendSeconds;
      } else if (v.job == Job::Fisherman) {
        // Face open water: away from the village center.
        glm::vec2 away = xz(v.pos) - xz(vil.center);
        if (glm::length(away) > 0.1f)
          v.yaw = std::atan2(away.x, away.y);
        v.state = VState::Work;
        v.workTimer = tune::kCastSeconds;
        v.workCount = 0;
      } else if (v.job == Job::Worshipper) {
        v.state = VState::Work;  // the dance is continuous (see update)
        v.workTimer = 1.0f;
      } else if (v.targetBuilding >= 0) {
        Building& b = vil.buildings[v.targetBuilding];
        bool atSite = glm::distance(xz(b.pos), xz(v.pos)) < 4.0f;
        if (atSite && b.type == BuildingType::Workshop && b.stage == 3) {
          // Crafting a scaffold at the bench.
          v.yaw = std::atan2(b.pos.x - v.pos.x, b.pos.z - v.pos.z);
          v.state = VState::Work;
          v.workTimer = tune::kScaffoldCraftSeconds;
        } else if (atSite) {
          v.yaw = std::atan2(b.pos.x - v.pos.x, b.pos.z - v.pos.z);
          v.state = VState::Work;  // deliver or hammer (decided on completion)
          v.workTimer = v.carriedProp >= 0 ? tune::kDepositSeconds : 0.45f;
        } else {
          // We're at the storage pad to withdraw a log.
          v.state = VState::Work;
          v.workTimer = tune::kPickupSeconds;
        }
      } else {
        v.state = VState::Idle;
        v.stateTimer = 0.5f;
      }
      break;
    }
    case VState::Panic:
      v.state = VState::Idle;
      v.stateTimer = 1.0f;
      break;
    default:
      break;
  }
}

void workCycleComplete(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  int myId = villagerId(vi, i);

  // Burial: the carried body is laid to rest at the graveyard.
  if (v.carriedProp >= 0 && w.props[v.carriedProp].type == PropType::Body) {
    int body = v.carriedProp;
    w.props[body].carrier = -1;
    v.carriedProp = -1;
    if (!vil.buryBody(w, body)) {
      // Graveyard gone or out of range: set the body down respectfully.
      w.props[body].asleep = false;
    }
    v.state = VState::Idle;
    v.stateTimer = 0.6f;
    v.thinkTimer = std::min(v.thinkTimer, 0.2f);
    return;
  }

  // Deposit at the storage pad (forester/farmer/fisherman hauling).
  if (v.state == VState::Work && v.carriedProp >= 0 &&
      vil.inStorageRadius(v.pos) &&
      (v.targetBuilding < 0 || v.job != Job::Builder)) {
    Prop& p = w.props[v.carriedProp];
    p.carrier = -1;
    vil.absorbProp(w, v.carriedProp);
    v.carriedProp = -1;
    v.state = VState::Idle;
    v.stateTimer = 0.2f;
    v.thinkTimer = std::min(v.thinkTimer, 0.15f);  // chain the next leg quickly
    return;
  }

  // Job-specific work.
  if (v.targetProp >= 0) {
    Prop& p = w.props[v.targetProp];
    if (p.type == PropType::Log || p.type == PropType::Food ||
        p.type == PropType::Body) {
      // Shoulder it: resources head for the pile, the dead for the graveyard.
      // A gift hurled here by a god is received - and remembered.
      if (p.thrownByGod >= 0 && p.type != PropType::Body) {
        w.notifyDivineEvent(p.thrownByGod, p.pos, 0.0f, tune::kAweGiftThrown);
        p.thrownByGod = -1;
      }
      p.carrier = myId;
      p.claimedBy = -1;
      v.carriedProp = v.targetProp;
      v.targetProp = -1;
      v.state = VState::Haul;
      if (p.type == PropType::Body) {
        int g = vil.completedGraveyard();
        v.moveTarget = g >= 0 ? xz(vil.buildings[g].pos)
                              : xz(vil.storagePos());
      } else {
        v.moveTarget = xz(vil.storagePos());
      }
      return;
    }
    if (p.type == PropType::Tree) {
      // One chop lands.
      p.resource -= 1.0f;
      ++v.workCount;
      // The trunk shudders with each strike.
      p.rot = glm::angleAxis(p.baseYaw + ((v.workCount & 1) ? 0.035f : -0.035f),
                             glm::vec3(0, 1, 0));
      if (p.resource <= 0.0f) {
        // Timber. The tree falls with real physics and must not replant.
        glm::vec2 away = xz(p.pos) - xz(v.pos);
        glm::vec3 d = glm::normalize(glm::vec3(away.x, 0.0f, away.y) +
                                     glm::vec3(0.001f, 0.0f, 0.0f));
        p.felled = true;
        p.asleep = false;
        p.uprighting = false;
        p.claimedBy = -1;
        p.angVel = glm::cross(glm::vec3(0, 1, 0), d) * 1.4f;
        p.vel = glm::vec3(0.0f);
        v.targetProp = -1;
        v.state = VState::Idle;  // wait for the logs to pop out
        v.stateTimer = 1.2f;
      } else {
        v.workTimer = tune::kChopSwingSeconds;  // next swing
      }
      return;
    }
  }

  if (v.targetCell >= 0) {
    FarmCell& c = vil.farmCells[v.targetCell];
    if (c.growth >= 1.0f) {
      c.growth = 0.0f;
      c.claimedBy = -1;
      v.targetCell = -1;
      spawnCarried(w, v, myId, PropType::Food, static_cast<float>(tune::kFoodPerHarvest));
      v.state = VState::Haul;
      v.moveTarget = xz(vil.storagePos());
    } else {
      c.tendedTimer = tune::kCropTendWindow;
      c.claimedBy = -1;
      v.targetCell = -1;
      v.state = VState::Idle;
      v.stateTimer = 0.2f;
      v.thinkTimer = std::min(v.thinkTimer, 0.15f);
    }
    return;
  }

  if (v.job == Job::Fisherman && v.targetProp < 0 && v.targetCell < 0 &&
      v.targetBuilding < 0) {
    // A cast finished - did anything bite?
    XorShift r(v.rng);
    bool caught = r.uniform() < tune::kCatchChance;
    v.rng = r.state;
    if (caught) ++v.workCount;
    if (v.workCount >= tune::kCatchesPerTrip) {
      spawnCarried(w, v, myId, PropType::Food, static_cast<float>(tune::kFoodPerCatch));
      v.state = VState::Haul;
      v.moveTarget = xz(vil.storagePos());
    } else {
      v.workTimer = tune::kCastSeconds;
    }
    return;
  }

  if (v.job == Job::Builder && v.targetBuilding >= 0) {
    Building& b = vil.buildings[v.targetBuilding];
    bool atSite = glm::distance(xz(b.pos), xz(v.pos)) < 4.5f;
    if (b.type == BuildingType::Workshop && b.stage == 3 && atSite) {
      // A scaffold comes off the bench.
      if (vil.wood >= tune::kScaffoldWoodCost &&
          looseScaffoldCount(w, vil) < tune::kMaxLooseScaffolds) {
        vil.wood -= tune::kScaffoldWoodCost;
        ++vil.scaffoldsCrafted;
        Prop s;
        s.type = PropType::Scaffold;
        s.scale = 1.0f;
        s.resource = 1.0f;
        s.radius = 0.9f + 0.18f;
        glm::vec3 side(std::sin(b.yaw + 1.5708f), 0.0f, std::cos(b.yaw + 1.5708f));
        s.pos = b.pos + side * 3.4f;
        s.pos.y = 0.0f;
        s.baseYaw = b.yaw;
        s.rot = glm::angleAxis(b.yaw, glm::vec3(0, 1, 0));
        s.asleep = true;
        int idx = w.spawnProp(s);
        w.props[idx].pos.y = w.restHeight(w.props[idx]);
      }
      v.targetBuilding = -1;
      v.state = VState::Idle;
      v.stateTimer = 0.4f;
      v.thinkTimer = std::min(v.thinkTimer, 0.2f);
      return;
    }
    if (atSite && v.carriedProp >= 0) {
      // Deliver the log to the site.
      Prop& p = w.props[v.carriedProp];
      p.carrier = -1;
      p.alive = false;
      v.carriedProp = -1;
      ++b.woodDelivered;
      v.state = VState::Idle;
      v.stateTimer = 0.2f;
      v.thinkTimer = std::min(v.thinkTimer, 0.15f);
    } else if (!atSite && v.carriedProp < 0) {
      // Withdraw a log from storage.
      if (vil.wood > 0) {
        --vil.wood;
        spawnCarried(w, v, myId, PropType::Log, 0.0f);
        v.state = VState::GoTo;
        v.moveTarget = xz(b.pos);
      } else {
        v.state = VState::Idle;
        v.stateTimer = 2.0f;
      }
    } else if (atSite) {
      // Hammering happens continuously in the Work state (see update);
      // reaching here means a hammer "cycle" elapsed - just keep going.
      v.workTimer = 0.45f;
    }
    return;
  }

  v.state = VState::Idle;
  v.stateTimer = 0.4f;
}

// Every hard contact funnels through this one function.
void applyLanding(World& w, Village& vil, int vi, int i, float impact) {
  Villager& v = vil.villagers[i];
  if (!tune::kVillagersInvulnerable && impact > tune::kLethalImpactSpeed) {
    villagerKill(w, vil, vi, i, DeathCause::Impact);
    return;
  }
  v.rot = glm::quat(1, 0, 0, 0);
  v.angVel = glm::vec3(0.0f);
  v.vel = glm::vec3(0.0f);
  v.pos.y = w.terrain.heightAt(v.pos.x, v.pos.z);

  if (impact > tune::kStunSpeed) {
    v.state = VState::Stunned;
    v.stateTimer = std::clamp(impact * 0.15f, 1.5f, 4.0f);
    v.fear = 1.0f;
    v.pendingAssign = false;
    return;
  }

  if (v.pendingAssign) {
    v.pendingAssign = false;
    Job j = vil.resolveJobAtPoint(w, v.pos);
    if (j != Job::None && j != v.job) {
      releaseClaims(w, vil, vi, i, v);
      v.job = j;
      v.assignedFlash = 1.6f;
    }
    v.state = VState::Idle;
    v.stateTimer = 0.3f;
    v.thinkTimer = std::min(v.thinkTimer, 0.1f);  // walk to work within seconds
    return;
  }

  if (v.fear > 0.55f) {
    v.state = VState::GetUp;
    v.stateTimer = 0.55f;
  } else {
    v.state = VState::Idle;
    v.stateTimer = 0.5f;
  }
}

// Context steering: probe a few headings, penalize water/cliffs/obstacles.
void steer(World& w, Village& vil, int vi, int i, float dt) {
  Villager& v = vil.villagers[i];
  glm::vec2 to = v.moveTarget - xz(v.pos);
  float dist = glm::length(to);
  if (dist < 1.8f) {
    arriveAtTarget(w, vil, vi, i);
    return;
  }
  float desired = std::atan2(to.x, to.y);
  bool targetInWater =
      w.terrain.heightAt(v.moveTarget.x, v.moveTarget.y) < Terrain::WATER_LEVEL + 0.4f;

  // Collect nearby solid obstacles once; probes test against this short list.
  struct Circle {
    glm::vec2 pos;
    float r2;
  };
  Circle obstacles[12];
  int obstacleCount = 0;
  w.forEachObstacleNear(xz(v.pos), [&](const Prop& p) {
    if (obstacleCount == 12) return;
    glm::vec2 d = xz(p.pos) - xz(v.pos);
    if (glm::dot(d, d) > 7.0f * 7.0f) return;
    float rr = p.radius * 0.8f + 0.5f;
    obstacles[obstacleCount++] = {xz(p.pos), rr * rr};
  });
  for (const God& god : w.gods) {
    if (!god.active || !god.temple.founded || obstacleCount >= 12) continue;
    glm::vec2 tp(god.temple.pos.x, god.temple.pos.z);
    glm::vec2 d = tp - xz(v.pos);
    if (glm::dot(d, d) < 10.0f * 10.0f)
      obstacles[obstacleCount++] = {tp, 4.5f * 4.5f};
  }

  static const float offsets[5] = {0.0f, -0.55f, 0.55f, -1.15f, 1.15f};
  float bestScore = -1.0e9f;
  float bestYaw = desired;
  for (float off : offsets) {
    float yaw = desired + off;
    glm::vec2 dir(std::sin(yaw), std::cos(yaw));
    glm::vec2 probe = xz(v.pos) + dir * 2.6f;
    float score = std::cos(off);
    float ph = w.terrain.heightAt(probe.x, probe.y);
    if (!targetInWater && ph < Terrain::WATER_LEVEL + 0.2f) score -= 4.0f;
    if (w.terrain.normalAt(probe.x, probe.y).y < 0.58f) score -= 3.0f;
    for (int c = 0; c < obstacleCount; ++c) {
      glm::vec2 d = probe - obstacles[c].pos;
      if (glm::dot(d, d) < obstacles[c].r2) {
        score -= 2.0f;
        break;
      }
    }
    for (std::size_t b = 0; b < vil.buildings.size(); ++b) {
      const Building& bd = vil.buildings[b];
      if (bd.stage < 0) continue;
      if (bd.type == BuildingType::Campfire || bd.type == BuildingType::Center)
        continue;
      if (static_cast<int>(b) == v.targetBuilding) continue;
      if (v.state == VState::Haul || v.state == VState::GoEat) {
        if (bd.type == BuildingType::Storage) continue;
      }
      if (v.state == VState::GoHome && static_cast<int>(b) == v.home) continue;
      glm::vec2 d = probe - xz(bd.pos);
      if (glm::dot(d, d) < 2.6f * 2.6f) {
        score -= 2.0f;
        break;
      }
    }
    if (score > bestScore) {
      bestScore = score;
      bestYaw = yaw;
    }
  }

  v.yaw = turnToward(v.yaw, bestYaw, tune::kTurnRate * dt);

  float slope = std::clamp((w.terrain.normalAt(v.pos.x, v.pos.z).y - 0.55f) / 0.35f,
                           0.3f, 1.0f);
  float speed = tune::kWalkSpeed * slope;
  if (v.state == VState::Panic) speed *= tune::kPanicSpeedFactor;
  if (v.carriedProp >= 0) speed *= tune::kCarrySpeedFactor;
  if (v.scale < 0.95f) speed *= 0.8f;
  if (v.hunger >= 0.999f) speed *= 0.5f;  // starving: listless

  glm::vec3 f(std::sin(v.yaw), 0.0f, std::cos(v.yaw));
  v.pos += f * speed * dt;
  v.pos.y = w.terrain.heightAt(v.pos.x, v.pos.z);
  v.walkPhase += speed * dt * 2.1f;

  // Stuck watchdog: no progress -> abandon the task, never teleport.
  v.progressTimer += dt;
  if (v.progressTimer > 3.0f) {
    if (glm::distance(xz(v.pos), v.lastProgressPos) < 0.6f) {
      ++vil.stuckEvents;
      v.blacklistProp = v.targetProp;
      v.blacklistTimer = 45.0f;
      releaseClaims(w, vil, vi, i, v);
      v.state = VState::Idle;
      v.stateTimer = 1.0f;
    }
    v.lastProgressPos = xz(v.pos);
    v.progressTimer = 0.0f;
  }
}

// The priority ladder, evaluated on the staggered think tick.
void think(World& w, Village& vil, int vi, int i) {
  Villager& v = vil.villagers[i];
  if (!isVoluntary(v.state)) return;
  float t = w.dayCycle.t;
  bool duskOrNight = t >= tune::kDuskT || t < tune::kDawnT - 0.02f;
  bool hardNight = t >= tune::kNightT || t < tune::kDawnT - 0.02f;

  // Rung 3: sleep schedule.
  if (duskOrNight) {
    if (v.state == VState::Sleep || v.state == VState::GoHome) return;
    if (v.state == VState::Work && !hardNight) return;  // finish the swing
    if (v.state == VState::Haul && !hardNight) return;  // finish the deposit
    dropCargo(w, v);
    releaseClaims(w, vil, vi, i, v);
    goHome(vil, v);
    return;
  }
  if (v.state == VState::Sleep) {
    if (v.energy > 0.85f) {
      v.state = VState::Idle;
      v.stateTimer = 1.2f;
    }
    return;
  }

  // Rung 4: hunger.
  bool eating = v.state == VState::GoEat || v.state == VState::Eat;
  if (!eating && vil.food > 0) {
    if (v.hunger > tune::kHungerUrgent) {
      dropCargo(w, v);
      releaseClaims(w, vil, vi, i, v);
      goEat(vil, v);
      return;
    }
    if (v.hunger > tune::kHungerWant &&
        (v.state == VState::Idle || v.state == VState::Wander ||
         v.state == VState::Chat)) {
      goEat(vil, v);
      return;
    }
  }
  if (eating) return;

  // Rung 4.5: exhaustion - sleep on the spot.
  if (v.energy < 0.10f) {
    dropCargo(w, v);
    releaseClaims(w, vil, vi, i, v);
    v.state = VState::Sleep;
    return;
  }

  // Rung 5: the dead must be buried. Any adult at a task boundary carries a
  // body to the graveyard (if one stands).
  if (v.scale > 0.9f && v.carriedProp < 0 &&
      (v.state == VState::Idle || v.state == VState::Wander ||
       v.state == VState::Chat) &&
      vil.completedGraveyard() >= 0) {
    int body = nearestProp(w, vil, v, villagerId(vi, i),
                           [](const Prop& p) { return p.type == PropType::Body; });
    if (body >= 0) {
      w.props[body].claimedBy = villagerId(vi, i);
      v.targetProp = body;
      v.state = VState::GoTo;
      v.moveTarget = xz(w.props[body].pos);
      return;
    }
  }

  // (Reserved rung: scheduled communal WORSHIP slots in exactly here.)

  // Rung 6: the job.
  if (v.job != Job::None && v.scale > 0.9f) {  // children don't work yet
    if (v.state == VState::Idle && v.stateTimer <= 0.0f) {
      planJob(w, vil, vi, i);
      return;
    }
    if (v.state == VState::GoTo || v.state == VState::Work ||
        v.state == VState::Haul) {
      validateJob(w, vil, vi, i);
      return;
    }
    if (v.state == VState::Wander || v.state == VState::Chat) return;  // brief break
    return;
  }

  // Rung 7: idle life.
  if (v.state == VState::Idle && v.stateTimer <= 0.0f) {
    XorShift r(v.rng);
    float roll = r.uniform();
    glm::vec3 anchor = v.home >= 0 ? vil.buildings[v.home].pos
                                   : vil.center;
    if (roll < 0.55f) {
      v.state = VState::Wander;
      v.moveTarget = xz(anchor) + glm::vec2(r.range(-1.0f, 1.0f), r.range(-1.0f, 1.0f)) *
                                      r.range(6.0f, 20.0f);
    } else if (roll < 0.8f) {
      v.stateTimer = r.range(2.0f, 6.0f);
    } else {
      // Chat: find another idle adult nearby.
      int partner = -1;
      for (std::size_t o = 0; o < vil.villagers.size(); ++o) {
        if (static_cast<int>(o) == i) continue;
        Villager& u = vil.villagers[o];
        if (!u.alive || u.state != VState::Idle || u.inside || u.held) continue;
        if (glm::distance(xz(u.pos), xz(v.pos)) < 9.0f) {
          partner = static_cast<int>(o);
          break;
        }
      }
      if (partner >= 0) {
        Villager& u = vil.villagers[partner];
        float dur = r.range(3.5f, 6.0f);
        v.state = VState::Chat;
        v.stateTimer = dur;
        u.state = VState::Chat;
        u.stateTimer = dur;
        v.yaw = std::atan2(u.pos.x - v.pos.x, u.pos.z - v.pos.z);
        u.yaw = std::atan2(v.pos.x - u.pos.x, v.pos.z - u.pos.z);
      } else {
        v.stateTimer = r.range(1.5f, 4.0f);
      }
    }
    v.rng = r.state;
  }
}

void updateVillage(World& world, Village& vil, int vi, float dt) {
  auto& vs = vil.villagers;
  const float dayFrac = dt / world.dayCycle.secondsPerDay;
  const float t = world.dayCycle.t;

  // Every god's hand frightens: the player's (fed by the app) and any AI
  // god's embodied one. Each villager reacts to whichever looms nearest.
  struct HandSense {
    glm::vec2 at;
    float y;
    float alt;
    bool swoop;
  };
  HandSense hands[1 + tune::kMaxGods];
  int handCount = 0;
  {
    float hg = world.terrain.heightAt(world.handPos.x, world.handPos.z);
    hands[handCount++] = {glm::vec2(world.handPos.x, world.handPos.z),
                          world.handPos.y, world.handPos.y - hg,
                          world.handSpeed > 18.0f};
    for (int g = 0; g < tune::kMaxGods; ++g) {
      if (!world.gods[g].active || !world.gods[g].ai) continue;
      const glm::vec3& hp = world.ai[g].handPos;
      if (hp.y > 1.0e8f) continue;  // resting out of the world
      float ag = world.terrain.heightAt(hp.x, hp.z);
      hands[handCount++] = {glm::vec2(hp.x, hp.z), hp.y, hp.y - ag,
                            glm::length(world.ai[g].handVel) > 18.0f};
    }
  }

  for (int i = 0; i < static_cast<int>(vs.size()); ++i) {
    Villager& v = vs[i];
    if (!v.alive) continue;  // the dead keep their slot, nothing more

    // Children grow.
    if (v.scale < 1.0f)
      v.scale = std::min(1.0f, v.scale + dayFrac * (1.0f - tune::kChildScale) /
                                             tune::kChildGrowDays);

    // Needs. Dancing for a god is hard work.
    bool sleeping = v.state == VState::Sleep;
    bool worshipping = v.job == Job::Worshipper && v.state == VState::Work;
    if (v.state != VState::Eat)
      v.hunger = std::min(1.0f, v.hunger + tune::kHungerPerDay * dayFrac *
                                    (worshipping ? tune::kWorshipHungerFactor : 1.0f));

    // Starvation: pinned at 1.0 with nothing to eat, a villager wastes away.
    // (Held villagers are preserved by the divine grip.)
    if (!tune::kVillagersInvulnerable && v.hunger >= 0.999f && !v.held) {
      v.starveTimer += dayFrac;
      if (v.starveTimer > tune::kStarveDays) {
        villagerKill(world, vil, vi, i, DeathCause::Starved);
        continue;
      }
    } else {
      v.starveTimer = std::max(0.0f, v.starveTimer - dayFrac * 2.0f);
    }
    if (sleeping)
      v.energy = std::min(1.0f, v.energy + tune::kEnergyRestorePerDay * dayFrac);
    else
      v.energy = std::max(0.0f, v.energy - tune::kEnergyDrainPerDay * dayFrac *
                                    (worshipping ? tune::kWorshipEnergyFactor : 1.0f));
    v.fear = std::max(0.0f, v.fear - tune::kFearDecayPerSec * dt);
    v.assignedFlash = std::max(0.0f, v.assignedFlash - dt);
    if (v.blacklistTimer > 0.0f) {
      v.blacklistTimer -= dt;
      if (v.blacklistTimer <= 0.0f) v.blacklistProp = -1;
    }

    // Hand awareness (head tracking + cower + flinch): the nearest hand.
    int nearHand = 0;
    float handDist = 1.0e9f;
    for (int hi = 0; hi < handCount; ++hi) {
      float d = glm::distance(hands[hi].at, xz(v.pos));
      if (d < handDist) {
        handDist = d;
        nearHand = hi;
      }
    }
    const HandSense& hand = hands[nearHand];
    float reactR = std::clamp(hand.alt * tune::kReactRadiusPerAltitude, 6.0f, 18.0f);
    bool handNear = handDist < reactR && std::abs(hand.y - v.pos.y) < 30.0f;
    v.lookAtHand = !v.inside && handNear;
    if (v.lookAtHand) {
      float want = wrapAngle(std::atan2(hand.at.x - v.pos.x,
                                        hand.at.y - v.pos.z) -
                             v.yaw);
      want = std::clamp(want, -1.1f, 1.1f);
      v.headLook += (want - v.headLook) * std::min(1.0f, 8.0f * dt);
    } else {
      v.headLook += (0.0f - v.headLook) * std::min(1.0f, 8.0f * dt);
    }

    if (v.held) {
      v.walkPhase += dt * 9.0f;  // wriggle in the palm
      continue;                  // the hand drives the position
    }
    int myId = villagerId(vi, i);

    if (v.inside) {
      // Sleeping in the house; emerge after dawn, staggered per villager.
      if (t > tune::kDawnT && t < 0.5f) {
        XorShift peek(v.rng);
        float stagger = peek.uniform() * 0.05f;  // don't consume the stream
        if (t > tune::kDawnT + stagger) {
          v.inside = false;
          v.state = VState::Idle;
          v.stateTimer = 1.8f;  // morning stretch
          if (v.home >= 0) {
            v.pos = doorPos(vil.buildings[v.home]);
            v.pos.y = world.terrain.heightAt(v.pos.x, v.pos.z);
            v.yaw = vil.buildings[v.home].yaw;
          }
        }
      }
      continue;
    }

    // A carried prop rides in the arms.
    if (v.carriedProp >= 0) {
      Prop& c = world.props[v.carriedProp];
      if (!c.alive || c.held || c.carrier != myId) {
        // The hand snatched it (or it vanished): react, re-plan.
        v.carriedProp = -1;
        if (isVoluntary(v.state)) {
          v.state = VState::Idle;
          v.stateTimer = 1.2f;
          v.fear = std::min(1.0f, v.fear + 0.3f);
        }
      } else {
        glm::vec3 f(std::sin(v.yaw), 0.0f, std::cos(v.yaw));
        c.pos = v.pos + f * 0.42f * v.scale + glm::vec3(0, 1.18f * v.scale, 0);
        c.rot = glm::angleAxis(v.yaw + 1.5708f, glm::vec3(0, 1, 0));
        c.vel = glm::vec3(0.0f);
        c.asleep = true;
      }
    }

    // Fear response: cower when a hand looms and trauma is fresh.
    if (isVoluntary(v.state) && v.fear > 0.35f && handNear &&
        (hand.alt < 14.0f || hand.swoop)) {
      dropCargo(world, v);
      v.state = VState::Cower;
      v.stateTimer = 0.9f;
    }

    // Think tick (staggered, deterministic order).
    v.thinkTimer -= dt;
    if (v.thinkTimer <= 0.0f) {
      v.thinkTimer += tune::kThinkInterval;
      think(world, vil, vi, i);
    }

    // Continuous per-state behavior.
    switch (v.state) {
      case VState::Airborne: {
        v.vel.y -= kGravity * dt;
        v.pos += v.vel * dt;
        integrateTumble(v.rot, v.angVel, dt);
        if (v.pos.y + 0.6f * v.scale < Terrain::WATER_LEVEL &&
            world.terrain.heightAt(v.pos.x, v.pos.z) < Terrain::WATER_LEVEL - 0.8f) {
          v.state = VState::Swim;
          v.vel *= 0.25f;
          v.rot = glm::quat(1, 0, 0, 0);
          v.angVel = glm::vec3(0.0f);
          v.submergedTime = 0.0f;
          break;
        }
        bool onGround = false;
        float impact = collideSphereTerrain(
            v.pos, v.vel, v.angVel, tune::kVillagerRadius, 0.05f, world.terrain,
            tune::kVillagerRestitution, 0.45f, 0.8f, onGround);
        if (onGround && (impact > 0.01f || glm::length(v.vel) < 0.8f))
          applyLanding(world, vil, vi, i, impact);
        break;
      }
      case VState::Swim: {
        v.submergedTime += dt;
        if (!tune::kVillagersInvulnerable &&
            v.submergedTime > tune::kDrownSeconds) {
          villagerKill(world, vil, vi, i, DeathCause::Drowned);
          break;
        }
        float targetY = Terrain::WATER_LEVEL - 1.15f * v.scale;
        v.pos.y += (targetY - v.pos.y) * std::min(1.0f, 4.0f * dt);
        glm::vec2 to = xz(vil.center) - xz(v.pos);
        if (glm::length(to) > 1.0f)
          v.yaw = turnToward(v.yaw, std::atan2(to.x, to.y),
                             tune::kTurnRate * 0.6f * dt);
        glm::vec3 f(std::sin(v.yaw), 0.0f, std::cos(v.yaw));
        v.pos += f * tune::kSwimSpeed * dt;
        v.walkPhase += dt * 5.5f;
        float ground = world.terrain.heightAt(v.pos.x, v.pos.z);
        if (ground > Terrain::WATER_LEVEL - 0.6f) {
          v.pos.y = ground;
          v.submergedTime = 0.0f;
          applyLanding(world, vil, vi, i, 0.0f);
        }
        break;
      }
      case VState::Stunned:
        v.stateTimer -= dt;
        if (v.stateTimer <= 0.0f) {
          v.state = VState::GetUp;
          v.stateTimer = 0.55f;
        }
        break;
      case VState::GetUp:
        v.stateTimer -= dt;
        if (v.stateTimer <= 0.0f) {
          if (v.fear > 0.55f) {
            v.state = VState::Panic;
            v.stateTimer = tune::kPanicSeconds;
            glm::vec2 away = xz(v.pos) - hand.at;  // flee the nearest hand
            float len = glm::length(away);
            away = len > 0.5f ? away / len
                              : glm::normalize(xz(vil.center) - xz(v.pos));
            v.moveTarget = xz(v.pos) + away * 26.0f;
          } else {
            v.state = VState::Idle;
            v.stateTimer = 0.6f;
          }
        }
        break;
      case VState::Cower:
        v.stateTimer -= dt;
        if (v.stateTimer <= 0.0f && (!handNear || v.fear < 0.3f)) {
          v.state = VState::Idle;
          v.stateTimer = 0.5f;
        }
        break;
      case VState::Panic:
        v.stateTimer -= dt;
        if (v.stateTimer <= 0.0f) {
          v.state = VState::Idle;
          v.stateTimer = 1.0f;
        } else {
          steer(world, vil, vi, i, dt);
        }
        break;
      case VState::Work:
        // Worship is a continuous circling dance, not a timed cycle: the
        // dancer orbits the totem, generating mana and sustaining belief.
        if (v.job == Job::Worshipper && vil.centerIdx >= 0) {
          const Building& totem = vil.buildings[vil.centerIdx];
          v.danceAngle += tune::kWorshipDanceRate * dt;
          glm::vec2 ring = xz(totem.pos) +
                           glm::vec2(std::sin(v.danceAngle), std::cos(v.danceAngle)) *
                               tune::kWorshipDanceRadius;
          v.pos.x += (ring.x - v.pos.x) * std::min(1.0f, 4.0f * dt);
          v.pos.z += (ring.y - v.pos.z) * std::min(1.0f, 4.0f * dt);
          v.pos.y = world.terrain.heightAt(v.pos.x, v.pos.z);
          v.yaw = std::atan2(totem.pos.x - v.pos.x, totem.pos.z - v.pos.z);
          v.walkPhase += dt * 4.2f;
          if (vil.owner >= 0 && world.gods[vil.owner].active) {
            God& god = world.gods[vil.owner];
            float mult = (0.5f + 1.5f * vil.belief[vil.owner]) *
                         vil.centerManaMultiplier();
            float add = tune::kManaPerWorshipperPerDay * mult * dayFrac;
            float space = god.manaMax - god.mana;
            if (add <= space) {
              god.mana += add;
            } else {
              // Pool full: the overflow charges a Miracle Dispenser instead.
              god.mana = god.manaMax;
              for (Building& b : vil.buildings) {
                if (b.type != BuildingType::Dispenser || b.stage != 3) continue;
                vil.dispenserFill += add - space;
                while (vil.dispenserFill >= tune::kFoodMiracleCost &&
                       b.charges < tune::kDispenserMaxCharges) {
                  vil.dispenserFill -= tune::kFoodMiracleCost;
                  ++b.charges;
                }
                break;
              }
            }
          }
          if (vil.owner >= 0)
            vil.belief[vil.owner] =
                std::min(1.0f, vil.belief[vil.owner] +
                                   tune::kBeliefFromWorshipPerDay * dayFrac);
          break;
        }
        v.workTimer -= dt;
        // Builders hammering advance the site continuously.
        if (v.job == Job::Builder && v.targetBuilding >= 0 && v.carriedProp < 0) {
          Building& b = vil.buildings[v.targetBuilding];
          if (b.stage >= 0 && b.stage < 3 && b.woodDelivered >= b.woodCost &&
              glm::distance(xz(b.pos), xz(v.pos)) < 4.5f) {
            b.buildProgress += dt;
            float required = b.tier > 0
                                 ? static_cast<float>(b.tier) * tune::kBuildSecondsPerScaffold
                                 : tune::kHouseBuildSeconds;
            int stage = std::min(2, static_cast<int>(3.0f * b.buildProgress / required));
            b.stage = std::max(b.stage, stage);
            if (b.buildProgress >= required) {
              int done = v.targetBuilding;
              v.state = VState::Idle;
              v.stateTimer = 0.5f;
              v.targetBuilding = -1;
              vil.onBuildingComplete(world, done);
            }
            // (onBuildingComplete homes only the living - the dead keep
            // no beds.)
          }
        }
        if (v.state == VState::Work && v.workTimer <= 0.0f)
          workCycleComplete(world, vil, vi, i);
        break;
      case VState::Eat:
        v.stateTimer -= dt;
        if (v.stateTimer <= 0.0f) {
          if (vil.food > 0) {
            --vil.food;
            ++vil.mealsEaten;
            v.hunger = tune::kHungerAfterMeal;
          }
          v.state = VState::Idle;
          v.stateTimer = 1.0f;
        }
        break;
      case VState::Chat:
        v.stateTimer -= dt;
        if (v.stateTimer <= 0.0f) {
          v.state = VState::Idle;
          v.stateTimer = 0.8f;
        }
        break;
      case VState::Idle:
        v.stateTimer -= dt;
        v.walkPhase += dt * 0.6f;
        break;
      case VState::Sleep:
        break;
      default:
        if (isWalking(v.state)) steer(world, vil, vi, i, dt);
        break;
    }

    // Soft villager-villager separation (cosmetic de-clumping).
    if (isVoluntary(v.state) && !v.inside) {
      for (std::size_t o = 0; o < vs.size(); ++o) {
        if (static_cast<int>(o) == i) continue;
        Villager& u = vs[o];
        if (!u.alive || u.inside || u.held) continue;
        glm::vec2 d = xz(v.pos) - xz(u.pos);
        float dd = glm::dot(d, d);
        if (dd > 0.0001f && dd < 1.1f * 1.1f) {
          glm::vec2 push = d / std::sqrt(dd);
          v.pos.x += push.x * 1.2f * dt;
          v.pos.z += push.y * 1.2f * dt;
        }
      }
    }
  }
}

}  // namespace

void villagersUpdate(World& world, float dt) {
  for (std::size_t v = 0; v < world.villages.size(); ++v)
    if (world.villages[v].founded)
      updateVillage(world, world.villages[v], static_cast<int>(v), dt);
}

void villagerGrabbed(World& world, int villageIdx, int idx, int god) {
  Village& vil = world.villages[villageIdx];
  Villager& v = vil.villagers[idx];
  v.held = true;
  v.inside = false;
  v.state = VState::Held;
  dropCargo(world, v);
  releaseClaims(world, vil, villageIdx, idx, v);
  v.fear = 1.0f;
  v.pendingAssign = false;
  world.notifyDivineEvent(god, v.pos, 0.55f, tune::kAweGrab);
}

void villagerReleased(World& world, int villageIdx, int idx,
                      const glm::vec3& velocity, bool gentle, int god) {
  Village& vil = world.villages[villageIdx];
  Villager& v = vil.villagers[idx];
  v.held = false;
  v.state = VState::Airborne;
  glm::vec3 vel = velocity;
  float speed = glm::length(vel);
  if (speed > tune::kMaxThrowSpeed) vel *= tune::kMaxThrowSpeed / speed;
  v.vel = vel;
  v.pendingAssign = gentle;
  if (gentle) {
    v.rot = glm::angleAxis(v.yaw, glm::vec3(0, 1, 0));
    v.angVel = glm::vec3(0.0f);
  } else {
    v.fear = 1.0f;
    glm::vec3 spinAxis = glm::cross(
        glm::normalize(vel + glm::vec3(0, 0.001f, 0)), glm::vec3(0, 1, 0));
    v.angVel = spinAxis * std::min(speed * 0.12f, 5.0f);
    world.notifyDivineEvent(god, v.pos, 0.8f, tune::kAweThrow);
  }
}

GrabTarget pickTarget(const World& world, const glm::vec3& origin,
                      const glm::vec3& dir, float maxDist) {
  GrabTarget out;
  float bestT = maxDist;

  int prop = world.pickProp(origin, dir, maxDist);
  if (prop >= 0) {
    // Recover the hit distance for comparison with villagers.
    const Prop& p = world.props[prop];
    bestT = glm::distance(origin, p.pos);
    out.kind = GrabTarget::Kind::Prop;
    out.index = prop;
  }

  for (std::size_t vi = 0; vi < world.villages.size(); ++vi) {
    const Village& vil = world.villages[vi];
    for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
      const Villager& v = vil.villagers[i];
      if (!v.alive || v.held || v.inside) continue;
      glm::vec3 center = v.pos + glm::vec3(0, 0.95f * v.scale, 0);
      float r = 1.05f * v.scale;
      glm::vec3 oc = origin - center;
      float b = glm::dot(oc, dir);
      float c = glm::dot(oc, oc) - r * r;
      float disc = b * b - c;
      if (disc < 0.0f) continue;
      float t = -b - std::sqrt(disc);
      // Villagers win near-ties: grabbing the person you point at matters
      // more than the tree behind them.
      if (t > 0.0f && t < bestT * 1.15f && t < maxDist) {
        bestT = std::min(t, bestT);
        out.kind = GrabTarget::Kind::Villager;
        out.village = static_cast<int>(vi);
        out.index = static_cast<int>(i);
      }
    }
  }
  return out;
}

void convertFelledTree(World& world, int propIdx) {
  // Copy what we need first: spawnProp may reallocate the props vector.
  glm::vec3 treePos = world.props[propIdx].pos;
  glm::quat treeRot = world.props[propIdx].rot;
  float treeScale = world.props[propIdx].scale;
  float baseYaw = world.props[propIdx].baseYaw;

  XorShift rng(world.seed() ^ (static_cast<std::uint32_t>(propIdx) * 2654435761u));
  glm::vec3 axis = treeRot * glm::vec3(0, 1, 0);  // where the trunk points now
  int logs = std::max(1, static_cast<int>(std::lround(tune::kLogsPerTree * treeScale)));
  for (int k = 0; k < logs; ++k) {
    Prop log;
    log.type = PropType::Log;
    log.scale = std::clamp(treeScale, 0.8f, 1.3f);
    log.radius = 0.5f * log.scale;
    log.resource = 1.0f;
    log.pos = treePos + axis * (0.6f + 1.7f * static_cast<float>(k)) +
              glm::vec3(0, 0.8f, 0);
    log.vel = glm::vec3(rng.range(-1.5f, 1.5f), rng.range(1.0f, 2.6f),
                        rng.range(-1.5f, 1.5f));
    log.baseYaw = rng.range(0.0f, 6.2831f);
    log.rot = glm::angleAxis(log.baseYaw, glm::vec3(0, 1, 0));
    log.asleep = false;
    world.spawnProp(log);
  }

  Prop& s = world.props[propIdx];
  glm::vec3 base = treePos - axis * (World::kTreeHalfHeight * treeScale);
  s.type = PropType::Stump;
  s.felled = false;
  s.resource = 0.0f;
  s.claimedBy = -1;
  s.radius = 0.5f * s.scale;
  s.rot = glm::angleAxis(baseYaw, glm::vec3(0, 1, 0));
  s.pos = glm::vec3(base.x, 0.0f, base.z);
  s.pos.y = world.restHeight(s);
  s.vel = glm::vec3(0.0f);
  s.angVel = glm::vec3(0.0f);
  s.asleep = true;
  s.uprighting = false;
}

// ---------------------------------------------------------------- pose math

namespace {

glm::mat4 rotX(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(1, 0, 0)); }
glm::mat4 rotY(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)); }
glm::mat4 tr(float x, float y, float z) {
  return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
}

}  // namespace

VillagerPose computeVillagerPose(const Villager& v, float time) {
  VillagerPose P;
  float phase = v.walkPhase;
  float legSwing = 0.0f, legBase = 0.0f;
  float armSwingL = 0.0f, armSwingR = 0.0f;
  float armRaiseL = 0.0f, armRaiseR = 0.0f;
  float torsoPitch = 0.0f, rootY = 0.0f, rootPitch = 0.0f;
  float headPitch = 0.0f;

  auto walkCycle = [&](float amp) {
    legSwing = 0.55f * amp * std::sin(phase);
    armSwingL = -0.4f * amp * std::sin(phase);
    armSwingR = 0.4f * amp * std::sin(phase);
    rootY = 0.045f * std::fabs(std::sin(phase));
    torsoPitch = 0.08f + 0.02f * amp;
  };

  switch (v.state) {
    case VState::Wander:
    case VState::GoTo:
    case VState::GoEat:
    case VState::GoHome:
      walkCycle(1.0f);
      break;
    case VState::Haul:
      walkCycle(0.9f);
      armSwingL = armSwingR = 0.0f;
      armRaiseL = armRaiseR = -1.25f;  // arms cradle the cargo
      torsoPitch = 0.16f;
      break;
    case VState::Panic:
      walkCycle(1.35f);
      armSwingL = armSwingR = 0.0f;
      armRaiseL = armRaiseR = -2.5f;  // arms over the head, screaming
      break;
    case VState::Idle:
      rootY = 0.01f * std::sin(time * 1.7f + phase);
      armSwingL = 0.05f * std::sin(time * 1.3f);
      armSwingR = -0.05f * std::sin(time * 1.3f);
      break;
    case VState::Chat:
      armRaiseR = -0.5f + 0.35f * std::sin(time * 2.3f + phase);
      headPitch = 0.08f * std::sin(time * 1.8f);
      break;
    case VState::Work:
      switch (v.job) {
        case Job::Forester: {
          // Anticipation-snap chop: slow two-handed raise, fast strike.
          float p = 1.0f - std::clamp(v.workTimer / tune::kChopSwingSeconds, 0.0f, 1.0f);
          float arm;
          if (p < 0.62f)
            arm = -2.1f * (p / 0.62f);
          else if (p < 0.78f)
            arm = -2.1f + 2.6f * ((p - 0.62f) / 0.16f);
          else
            arm = 0.5f - 0.5f * ((p - 0.78f) / 0.22f);
          armRaiseL = armRaiseR = arm;
          torsoPitch = 0.12f - arm * 0.10f;
          break;
        }
        case Job::Farmer:
          torsoPitch = 0.5f;
          armRaiseR = -0.8f + 0.55f * std::sin(time * 4.0f);
          armRaiseL = -0.3f;
          break;
        case Job::Fisherman:
          armRaiseL = armRaiseR = -1.3f;
          rootY = 0.015f * std::sin(time * 1.2f);
          break;
        case Job::Builder:
          rootY = -0.18f;
          torsoPitch = 0.35f;
          armRaiseR = -1.0f + 0.5f * std::sin(time * 9.0f);
          break;
        case Job::Worshipper:
          // Ecstatic dance: both arms high, swaying, bouncing steps.
          armRaiseL = -2.6f + 0.35f * std::sin(time * 3.1f + phase);
          armRaiseR = -2.6f + 0.35f * std::sin(time * 3.1f + phase + 1.6f);
          legSwing = 0.35f * std::sin(phase);
          rootY = 0.07f * std::fabs(std::sin(phase));
          torsoPitch = 0.05f + 0.06f * std::sin(time * 2.1f);
          break;
        default:
          rootY = -0.3f;  // generic crouch (picking something up)
          torsoPitch = 0.5f;
          armRaiseL = armRaiseR = -0.6f;
          break;
      }
      break;
    case VState::Eat:
      rootY = -0.55f;
      legBase = -1.5f;  // sitting, legs out
      armRaiseR = -1.6f + 0.45f * std::sin(time * 3.0f);
      headPitch = 0.15f;
      break;
    case VState::Sleep:
      rootPitch = 1.5f;
      rootY = 0.35f;
      armRaiseL = armRaiseR = -0.3f;
      break;
    case VState::Held:
    case VState::Airborne: {
      float k = v.state == VState::Held ? 0.9f : 1.3f;
      float o = static_cast<float>(v.variant) * 2.1f;
      armSwingL = k * std::sin(time * 9.0f + o);
      armSwingR = k * std::sin(time * 11.0f + o + 1.6f);
      armRaiseL = -1.2f;
      armRaiseR = -1.2f;
      legSwing = 0.8f * k * std::sin(time * 12.0f + o);
      break;
    }
    case VState::Swim:
      torsoPitch = 1.0f;
      armSwingL = 1.1f * std::sin(phase * 1.4f);
      armSwingR = 1.1f * std::sin(phase * 1.4f + 3.1f);
      legSwing = 0.4f * std::sin(phase * 2.2f);
      headPitch = -0.5f;
      break;
    case VState::Stunned:
      rootPitch = 1.5f;
      rootY = 0.35f;
      armSwingL = 0.4f;
      armSwingR = -0.5f;
      legSwing = 0.2f;
      break;
    case VState::GetUp: {
      float p = 1.0f - std::clamp(v.stateTimer / 0.55f, 0.0f, 1.0f);
      rootPitch = 1.5f * (1.0f - p);
      rootY = 0.35f * (1.0f - p);
      break;
    }
    case VState::Cower:
      rootY = -0.35f;
      torsoPitch = 0.45f;
      armRaiseL = armRaiseR = -2.7f;  // arms over the head
      headPitch = 0.5f;
      break;
    default:
      break;
  }

  glm::mat4 base = v.state == VState::Airborne ? glm::mat4_cast(v.rot) : rotY(v.yaw);
  P.root = base * glm::scale(glm::mat4(1.0f), glm::vec3(v.scale)) *
           tr(0.0f, rootY, 0.0f) * rotX(rootPitch);

  P.legL = tr(-0.12f, 0.70f, 0.0f) * rotX(legBase + legSwing);
  P.legR = tr(0.12f, 0.70f, 0.0f) * rotX(legBase - legSwing);
  P.torso = tr(0.0f, 1.02f, 0.0f) * rotX(torsoPitch);
  P.armL = P.torso * tr(-0.33f, 0.26f, 0.0f) * rotX(armRaiseL + armSwingL);
  P.armR = P.torso * tr(0.33f, 0.26f, 0.0f) * rotX(armRaiseR + armSwingR);
  P.head = P.torso * tr(0.0f, 0.46f, 0.0f) * rotY(v.headLook) * rotX(headPitch);
  return P;
}
