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

  // Sparse islands starve the war of contested ground: a second, laxer
  // quality pass (same separation) tops up the site list before giving up.
  if (static_cast<int>(picked.size()) < count) {
    std::vector<Candidate> lax;
    for (float z = -lim; z <= lim; z += 8.0f) {
      for (float x = -lim; x <= lim; x += 8.0f) {
        float h = terrain.heightAt(x, z);
        if (h < 2.0f || h > 16.0f) continue;
        float flat = flatnessAt(x, z);
        if (flat < 0.84f) continue;
        lax.push_back({flat * 3.0f - std::abs(h - 5.0f) * 0.08f, {x, z}});
      }
    }
    std::stable_sort(lax.begin(), lax.end(), [](const Candidate& a, const Candidate& b) {
      return a.score > b.score;
    });
    for (const Candidate& c : lax) {
      if (static_cast<int>(picked.size()) >= count) break;
      bool clear = true;
      for (const glm::vec2& p : picked)
        if (glm::distance(p, c.pos) < tune::kVillageMinSeparation) clear = false;
      if (clear) picked.push_back(c.pos);
    }
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

void World::generate(std::uint32_t seed, int godCount) {
  terrain.generate(seed);
  generateOnCurrentTerrain(seed, godCount);
}

void World::generateOnCurrentTerrain(std::uint32_t seed, int godCount) {
  seed_ = seed;
  miracleCounter_ = 0;
  godCount = std::clamp(godCount, 1, tune::kMaxGods);

  // Found the player's home village on the best site. In a skirmish world the
  // rival takes the picked site farthest from it; the rest stay neutral. A
  // hostile island that yields a single site simply never wakes the rival.
  std::vector<glm::vec2> sites = findVillageSites(godCount + tune::kNeutralVillages);
  int rivalSite = -1;
  if (godCount >= 2 && sites.size() >= 2) {
    float bestD = -1.0f;
    for (std::size_t s = 1; s < sites.size(); ++s) {
      float d = glm::distance(sites[0], sites[s]);
      if (d > bestD) {
        bestD = d;
        rivalSite = static_cast<int>(s);
      }
    }
  }
  villages.clear();
  villages.resize(sites.size());
  for (std::size_t v = 0; v < sites.size(); ++v) {
    villages[v].owner =
        v == 0 ? 0 : (static_cast<int>(v) == rivalSite ? 1 : -1);
    bool hard = v == 0 && sites.size() == 1 &&
                terrain.heightAt(sites[0].x, sites[0].y) < 2.5f;
    villages[v].plan(*this, seed + static_cast<std::uint32_t>(v) * 7919u, sites[v],
                     hard);
  }

  for (God& g : gods) g = God{};
  gods[0].active = true;
  gods[0].isPlayer = true;
  gods[0].mana = tune::kManaStart;
  gods[0].manaMax = tune::kManaMax;
  if (rivalSite >= 0) {
    gods[1].active = true;
    gods[1].ai = true;
    gods[1].mana = tune::kManaStart;
    gods[1].manaMax = tune::kManaMax;
  }
  foundTemple(0, 0);           // also flattens; must precede prop scatter
  if (rivalSite >= 0) foundTemple(1, rivalSite);
  scatterProps();
  for (std::size_t v = 0; v < villages.size(); ++v)
    villages[v].spawnVillagers(*this, seed, static_cast<int>(v));
  dayCycle = DayCycle{};
  handPos = glm::vec3(0.0f, 1.0e9f, 0.0f);
  handSpeed = 0.0f;
  obstacleGrid_.assign(kObstacleGridN * kObstacleGridN, {});
  for (int g = 0; g < tune::kMaxGods; ++g) ai[g].reset(*this, g);
}

bool World::godBroken(int god) const {
  if (god < 0 || god >= tune::kMaxGods || !gods[god].active) return false;
  for (const Village& v : villages)
    if (v.founded && v.owner == god) return false;
  return true;
}

void World::buildBlank(std::uint32_t seed) {
  seed_ = seed;
  miracleCounter_ = 0;
  editStroke_ = 0;
  terrain.generateBlank(seed);
  villages.clear();
  props.clear();
  for (God& g : gods) g = God{};
  gods[0].active = true;
  gods[0].isPlayer = true;
  gods[0].mana = tune::kManaStart;
  gods[0].manaMax = tune::kManaMax;
  dayCycle = DayCycle{};
  handPos = glm::vec3(0.0f, 1.0e9f, 0.0f);
  handSpeed = 0.0f;
  obstacleGrid_.assign(kObstacleGridN * kObstacleGridN, {});
  for (int g = 0; g < tune::kMaxGods; ++g) ai[g].reset(*this, g);
}

void World::wakeGod(int god) {
  if (god < 0 || god >= tune::kMaxGods) return;
  God& g = gods[god];
  if (g.active) return;
  g.active = true;
  g.isPlayer = god == 0;
  g.mana = tune::kManaStart;
  g.manaMax = tune::kManaMax;
  if (god != 0) g.ai = true;
  ai[god].reset(*this, god);
}

int World::foundVillageFromSpec(glm::vec2 site, int owner, int preset,
                                bool terraform) {
  int idx = static_cast<int>(villages.size());
  villages.emplace_back();
  Village& v = villages.back();
  v.owner = std::clamp(owner, -1, tune::kMaxGods - 1);
  v.startPreset = std::clamp(preset, 0, 2);
  v.plan(*this, seed_ + static_cast<std::uint32_t>(idx) * 7919u, site, false,
         terraform);
  v.spawnVillagers(*this, seed_, idx, tune::kEditorPresetPop[v.startPreset]);
  v.food = tune::kEditorPresetFood[v.startPreset];
  v.wood = tune::kEditorPresetWood[v.startPreset];
  if (v.owner >= 0) wakeGod(v.owner);
  return idx;
}

int World::editorPlaceVillage(glm::vec2 site, int owner, int preset) {
  if (terrain.heightAt(site.x, site.y) < 1.5f) return -1;
  for (const Village& v : villages)
    if (v.founded &&
        glm::distance(glm::vec2(v.center.x, v.center.z), site) <
            tune::kEditorVillageSeparation)
      return -1;
  return foundVillageFromSpec(site, owner, preset, true);
}

void World::editorPlaceTemple(int god, glm::vec2 pos) {
  if (god < 0 || god >= tune::kMaxGods) return;
  God& g = gods[god];
  float targetH = std::clamp(terrain.heightAt(pos.x, pos.y), 2.5f, 14.0f);
  terrain.flattenDisc(pos.x, pos.y, 16.0f, targetH, 0.95f);
  g.temple.founded = true;
  g.temple.pos = glm::vec3(pos.x, terrain.heightAt(pos.x, pos.y), pos.y);
  // Face the god's nearest village (or the island heart on an empty map).
  glm::vec2 face(0.0f);
  float best = 1.0e9f;
  for (const Village& v : villages) {
    if (!v.founded || v.owner != god) continue;
    float d = glm::distance(glm::vec2(v.center.x, v.center.z), pos);
    if (d < best) {
      best = d;
      face = glm::vec2(v.center.x, v.center.z);
    }
  }
  g.temple.yaw = noise::atan2det(face.x - pos.x, face.y - pos.y);
  wakeGod(god);
  ai[god].reset(*this, god);
}

void World::editorPaintForest(glm::vec2 center, float radius) {
  XorShift rng(seed_ ^ (0xF0537u + (++editStroke_) * 2654435761u));
  int want = 1 + static_cast<int>(radius / 7.0f);
  for (int attempt = 0; attempt < 24 && want > 0; ++attempt) {
    glm::vec2 p = center + glm::vec2(rng.range(-radius, radius),
                                     rng.range(-radius, radius));
    if (glm::distance(p, center) > radius) continue;
    float h = terrain.heightAt(p.x, p.y);
    if (h < 1.8f || terrain.normalAt(p.x, p.y).y < 0.75f) continue;
    bool blocked = false;
    for (const Prop& q : props) {
      if (!q.alive) continue;
      if (q.type != PropType::Tree && q.type != PropType::Rock &&
          q.type != PropType::Stump)
        continue;
      if (glm::distance(glm::vec2(q.pos.x, q.pos.z), p) < 3.5f) blocked = true;
    }
    for (const Village& v : villages)
      blocked |= v.founded && v.insideFootprint(p.x, p.y);
    for (const God& g : gods)
      blocked |= g.temple.founded &&
                 glm::distance(glm::vec2(g.temple.pos.x, g.temple.pos.z), p) < 18.0f;
    if (blocked) continue;

    Prop t;
    t.type = PropType::Tree;
    t.variant = static_cast<int>(rng.next() % 3u);
    t.scale = rng.range(0.8f, 1.35f);
    t.radius = 1.6f * t.scale;
    t.baseYaw = rng.range(0.0f, 6.2831f);
    t.resource = static_cast<float>(tune::kChopSwings);
    t.pos = glm::vec3(p.x, 0.0f, p.y);
    t.pos.y = restHeight(t);
    t.rot = glm::angleAxis(t.baseYaw, glm::vec3(0, 1, 0));
    spawnProp(t);
    --want;
  }
}

void World::editorPaintRocks(glm::vec2 center, float radius) {
  XorShift rng(seed_ ^ (0x50CC5u + (++editStroke_) * 2654435761u));
  int want = 1 + static_cast<int>(radius / 14.0f);
  for (int attempt = 0; attempt < 18 && want > 0; ++attempt) {
    glm::vec2 p = center + glm::vec2(rng.range(-radius, radius),
                                     rng.range(-radius, radius));
    if (glm::distance(p, center) > radius) continue;
    if (terrain.heightAt(p.x, p.y) < -3.0f) continue;  // shallows are fine
    bool blocked = false;
    for (const Prop& q : props) {
      if (!q.alive) continue;
      if (q.type != PropType::Tree && q.type != PropType::Rock &&
          q.type != PropType::Stump)
        continue;
      if (glm::distance(glm::vec2(q.pos.x, q.pos.z), p) < 2.5f) blocked = true;
    }
    for (const Village& v : villages)
      blocked |= v.founded && v.insideFootprint(p.x, p.y);
    for (const God& g : gods)
      blocked |= g.temple.founded &&
                 glm::distance(glm::vec2(g.temple.pos.x, g.temple.pos.z), p) < 18.0f;
    if (blocked) continue;

    Prop r;
    r.type = PropType::Rock;
    r.variant = static_cast<int>(rng.next() % 3u);
    r.scale = rng.range(0.55f, 2.0f);
    r.radius = 0.9f * r.scale;
    r.baseYaw = rng.range(0.0f, 6.2831f);
    r.pos = glm::vec3(p.x, 0.0f, p.y);
    r.pos.y = restHeight(r);
    r.rot = glm::angleAxis(r.baseYaw, glm::vec3(0, 1, 0));
    spawnProp(r);
    --want;
  }
}

int World::editorEraseProps(glm::vec2 center, float radius) {
  int erased = 0;
  for (Prop& p : props) {
    if (!p.alive || p.held || p.carrier >= 0) continue;
    if (p.type != PropType::Tree && p.type != PropType::Rock &&
        p.type != PropType::Stump)
      continue;
    if (glm::distance(glm::vec2(p.pos.x, p.pos.z), center) > radius) continue;
    p.alive = false;
    p.claimedBy = -1;
    ++erased;
  }
  return erased;
}

void World::editorSnapToGround(glm::vec2 center, float radius) {
  float pad = radius + 8.0f;
  for (Prop& p : props) {
    if (!p.alive || p.held || p.carrier >= 0) continue;
    if (glm::distance(glm::vec2(p.pos.x, p.pos.z), center) > pad) continue;
    p.pos.y = restHeight(p);
    p.vel = glm::vec3(0.0f);
    p.asleep = true;
  }
  for (Village& v : villages) {
    if (!v.founded) continue;
    for (Building& b : v.buildings) {
      if (glm::distance(glm::vec2(b.pos.x, b.pos.z), center) > pad) continue;
      b.pos.y = terrain.heightAt(b.pos.x, b.pos.z);
    }
    for (Villager& p : v.villagers) {
      if (!p.alive || p.held || p.inside) continue;
      if (glm::distance(glm::vec2(p.pos.x, p.pos.z), center) > pad) continue;
      p.pos.y = terrain.heightAt(p.pos.x, p.pos.z);
    }
    if (glm::distance(glm::vec2(v.center.x, v.center.z), center) <= pad)
      v.center.y = terrain.heightAt(v.center.x, v.center.z);
  }
  for (God& g : gods) {
    if (!g.temple.founded) continue;
    if (glm::distance(glm::vec2(g.temple.pos.x, g.temple.pos.z), center) > pad)
      continue;
    g.temple.pos.y = terrain.heightAt(g.temple.pos.x, g.temple.pos.z);
  }
}

void World::foundTemple(int god, int villageIdx) {
  if (villageIdx < 0 || villageIdx >= static_cast<int>(villages.size()) ||
      !villages[villageIdx].founded)
    return;
  const Village& village = villages[villageIdx];
  Temple& temple = gods[god].temple;
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
  temple.yaw = noise::atan2det(village.center.x - best.x, village.center.z - best.y);
}

bool World::insideInfluence(const glm::vec3& p, int god) const {
  if (god < 0 || god >= tune::kMaxGods || !gods[god].active) return false;
  const Temple& temple = gods[god].temple;
  if (temple.founded &&
      glm::distance(glm::vec2(p.x, p.z), glm::vec2(temple.pos.x, temple.pos.z)) <
          tune::kTempleInfluence)
    return true;
  for (const Village& v : villages) {
    if (!v.founded || v.owner != god) continue;  // neutrals project nothing
    if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(v.center.x, v.center.z)) <
        v.influenceRadius())
      return true;
  }
  return false;
}

void World::notifyDivineEvent(int god, const glm::vec3& where, float fear,
                              float awe) {
  for (Village& v : villages) v.notifyDivineEvent(god, where, fear, awe);
}

// The conversion ratchet. Neutral villages join a god whose standing clearly
// leads; owned villages are stolen only by overwhelming faith over a lapsed
// owner - mostly forward progress, real defense.
void World::updateOwnership() {
  for (std::size_t vi = 0; vi < villages.size(); ++vi) {
    Village& v = villages[vi];
    if (!v.founded) continue;
    // The strongest challenger.
    int best = -1;
    float bestB = 0.0f, secondB = 0.0f;
    for (int g = 0; g < tune::kMaxGods; ++g) {
      // A ruined god is out of the war: its faith cannot claim villages.
      if (!gods[g].active || gods[g].ruined || g == v.owner) continue;
      if (v.belief[g] > bestB) {
        secondB = bestB;
        bestB = v.belief[g];
        best = g;
      } else {
        secondB = std::max(secondB, v.belief[g]);
      }
    }
    if (best < 0) continue;
    if (v.owner < 0) {
      if (bestB > tune::kConvertNeutralBelief &&
          bestB > secondB + tune::kConvertLeadMargin)
        convertVillage(static_cast<int>(vi), best);
    } else {
      if (bestB > tune::kStealBelief &&
          v.belief[v.owner] < tune::kStealOwnerBelow) {
        int prevOwner = v.owner;
        convertVillage(static_cast<int>(vi), best);
        // A god stripped of its last village is broken: the temple falls.
        if (prevOwner >= 0 && !gods[prevOwner].ruined && godBroken(prevOwner))
          collapseTemple(prevOwner, best);
      }
    }
  }
}

void World::collapseTemple(int god, int conqueror) {
  God& g = gods[god];
  g.ruined = true;
  g.mana = 0.0f;
  if (!g.temple.founded) return;
  glm::vec3 at = g.temple.pos;
  g.temple.founded = false;

  XorShift rng(seed_ ^ (0xDEAD5EEDu + static_cast<std::uint32_t>(god) * 7919u));
  for (int k = 0; k < 10; ++k) {
    Prop r;
    r.type = PropType::Rock;
    r.variant = static_cast<int>(rng.next() % 3u);
    r.scale = rng.range(0.5f, 1.3f);
    r.radius = 0.9f * r.scale;
    r.baseYaw = rng.range(0.0f, 6.2831f);
    r.rot = glm::angleAxis(r.baseYaw, glm::vec3(0, 1, 0));
    r.pos = at + glm::vec3(rng.range(-1.6f, 1.6f), 2.5f + rng.range(0.0f, 2.2f),
                           rng.range(-1.6f, 1.6f));
    r.vel = glm::vec3(rng.range(-9.0f, 9.0f), rng.range(4.0f, 11.0f),
                      rng.range(-9.0f, 9.0f));
    r.angVel = glm::vec3(rng.range(-4.0f, 4.0f), rng.range(-4.0f, 4.0f),
                         rng.range(-4.0f, 4.0f));
    r.asleep = false;
    spawnProp(r);
  }
  // The island quakes; the conqueror's triumph is witnessed everywhere.
  notifyDivineEvent(-1, at, 0.6f, 0.0f);
  if (conqueror >= 0) notifyDivineEvent(conqueror, at, 0.0f, tune::kAweMiracle);
}

void World::convertVillage(int villageIdx, int newOwner) {
  Village& v = villages[villageIdx];
  v.owner = newOwner;
  // The new faith suppresses the others.
  for (int g = 0; g < tune::kMaxGods; ++g)
    if (g != newOwner) v.belief[g] *= 0.5f;
  // The ceremony: some villagers scatter in terror of their new master;
  // worship at the totem now feeds the new owner automatically.
  XorShift rng(seed_ ^ (static_cast<std::uint32_t>(villageIdx) * 2654435761u +
                        static_cast<std::uint32_t>(newOwner) * 97u));
  for (Villager& p : v.villagers) {
    if (!p.alive || p.inside) continue;
    if (rng.uniform() < tune::kConversionScatter) p.fear = 1.0f;
  }
  v.notifyDivineEvent(newOwner, v.center, 0.5f, 0.0f);
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

bool World::castFoodMiracle(const glm::vec3& p, int god) {
  if (god < 0 || god >= tune::kMaxGods || !gods[god].active) return false;
  if (!gods[god].temple.founded || !insideInfluence(p, god)) return false;

  // A charged Miracle Dispenser within reach covers the cost first.
  Building* dispenser = nullptr;
  for (Village& v : villages) {
    if (v.owner != god) continue;
    for (Building& b : v.buildings)
      if (b.type == BuildingType::Dispenser && b.stage == 3 && b.charges > 0 &&
          glm::distance(glm::vec2(b.pos.x, b.pos.z), glm::vec2(p.x, p.z)) <
              tune::kDispenserCastRadius)
        dispenser = &b;
  }

  if (dispenser) {
    --dispenser->charges;
  } else {
    if (gods[god].mana < tune::kFoodMiracleCost) return false;
    gods[god].mana -= tune::kFoodMiracleCost;
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
  notifyDivineEvent(god, p, 0.05f, tune::kAweMiracle);
  return true;
}

float World::miracleCost(Miracle kind) const {
  switch (kind) {
    case Miracle::Food: return tune::kFoodMiracleCost;
    case Miracle::Rain: return tune::kRainCost;
    case Miracle::Forest: return tune::kForestCost;
    case Miracle::Fireball: return tune::kFireballCost;
  }
  return 0.0f;
}

bool World::miracleUnlocked(Miracle kind, int god) const {
  if (kind == Miracle::Food) return true;
  BuildingType need = kind == Miracle::Fireball ? BuildingType::Wonder
                                                : BuildingType::Dispenser;
  for (const Village& v : villages)
    if (v.founded && v.owner == god && v.countCompleted(need) > 0) return true;
  return false;
}

float World::rainBoostAt(const glm::vec3& p) const {
  for (const RainCloud& r : rains)
    if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(r.pos.x, r.pos.z)) < r.radius)
      return tune::kRainGrowthBoost;
  return 1.0f;
}

// The one gate every spell walks through (M11): live god, founded temple,
// inside influence, unlocked, affordable. Belief flows only through
// notifyDivineEvent; villager harm only through the landing seam.
bool World::castMiracle(Miracle kind, const glm::vec3& p, int god) {
  if (god < 0 || god >= tune::kMaxGods || !gods[god].active) return false;
  if (!gods[god].temple.founded || !insideInfluence(p, god)) return false;
  if (!miracleUnlocked(kind, god)) return false;
  if (kind == Miracle::Food) {
    if (!castFoodMiracle(p, god)) return false;
    events.push_back({WorldEvent::Kind::MiracleCast, p,
                      static_cast<float>(Miracle::Food), god});
    return true;
  }
  if (gods[god].mana < miracleCost(kind)) return false;

  XorShift rng(seed_ ^ (++miracleCounter_ * 0x9E3779B9u));
  switch (kind) {
    case Miracle::Rain: {
      gods[god].mana -= tune::kRainCost;
      RainCloud r;
      r.pos = glm::vec3(p.x, terrain.heightAt(p.x, p.z), p.z);
      r.radius = tune::kRainRadius;
      r.duration = tune::kRainDuration;
      r.god = god;
      rains.push_back(r);
      notifyDivineEvent(god, p, 0.0f, tune::kAweRain);
      events.push_back({WorldEvent::Kind::MiracleCast, p,
                        static_cast<float>(Miracle::Rain), god});
      break;
    }
    case Miracle::Forest: {
      // Plant first, pay only for a grove that took root somewhere.
      int planted = 0;
      for (int attempt = 0; attempt < 60 && planted < tune::kForestTrees;
           ++attempt) {
        float ox = rng.range(-1.0f, 1.0f) * tune::kForestRadius;
        float oz = rng.range(-1.0f, 1.0f) * tune::kForestRadius;
        if (ox * ox + oz * oz > tune::kForestRadius * tune::kForestRadius)
          continue;
        float x = p.x + ox, z = p.z + oz;
        float h = terrain.heightAt(x, z);
        if (h < 0.8f || h > 40.0f) continue;
        if (terrain.normalAt(x, z).y < 0.78f) continue;
        bool blocked = false;
        for (const Village& v : villages)
          blocked |= v.founded && v.insideFootprint(x, z);
        for (const Prop& other : props)
          if (!blocked && other.alive && other.type == PropType::Tree &&
              glm::distance(glm::vec2(x, z),
                            glm::vec2(other.pos.x, other.pos.z)) < 3.0f)
            blocked = true;
        if (blocked) continue;

        Prop t;
        t.type = PropType::Tree;
        t.variant = static_cast<int>(rng.next() % 3u);
        t.scale = rng.range(0.8f, 1.2f);
        t.radius = 1.6f * t.scale;
        t.baseYaw = rng.range(0.0f, 6.2831f);
        t.resource = static_cast<float>(tune::kChopSwings);
        t.pos = glm::vec3(x, 0.0f, z);
        t.pos.y = restHeight(t);
        t.rot = glm::angleAxis(t.baseYaw, glm::vec3(0, 1, 0));
        t.asleep = true;
        spawnProp(t);
        ++planted;
      }
      if (planted == 0) return false;  // ocean, cliff, or a full village
      gods[god].mana -= tune::kForestCost;
      notifyDivineEvent(god, p, 0.0f, tune::kAweForest);
      events.push_back({WorldEvent::Kind::ForestBloom, p,
                        static_cast<float>(planted), god});
      break;
    }
    case Miracle::Fireball: {
      gods[god].mana -= tune::kFireballCost;
      Fireball f;
      // From high over the shoulder, aimed to strike p in ~1.15 s.
      glm::vec3 start = p + glm::vec3(rng.range(-20.0f, 20.0f),
                                      34.0f + rng.range(0.0f, 8.0f),
                                      rng.range(-20.0f, 20.0f));
      const float flight = 1.15f;
      f.pos = start;
      f.vel = (p - start) / flight;
      f.vel.y = (p.y - start.y) / flight + 0.5f * kGravity * flight;
      f.god = god;
      fireballs.push_back(f);
      events.push_back({WorldEvent::Kind::MiracleCast, p,
                        static_cast<float>(Miracle::Fireball), god});
      break;
    }
    case Miracle::Food:
      break;  // handled above
  }
  return true;
}

// The comet lands: everything loose is hurled outward (villager deaths only
// ever via applyLanding), trees scorch to stumps in place, and the blast is
// witnessed as terror with a sliver of awe.
void World::explodeFireball(const Fireball& f) {
  const glm::vec3 at = f.pos;
  const float R = tune::kFireballRadius;
  for (Village& vil : villages) {
    for (Villager& v : vil.villagers) {
      // The divine grip preserves; walls (inside) shelter.
      if (!v.alive || v.held || v.inside) continue;
      glm::vec3 d = v.pos - at;
      float dist = glm::length(d);
      if (dist > R) continue;
      glm::vec3 dir = dist > 0.01f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
      dir.y += 0.85f;
      dir = glm::normalize(dir);
      float power = tune::kFireballImpulse * (1.0f - dist / R) + 5.0f;
      v.state = VState::Airborne;
      v.pendingAssign = false;
      v.fear = 1.0f;
      v.vel = dir * power;
      v.pos.y += 0.35f;  // unstick so the hurl takes
      glm::vec3 spinAxis =
          glm::cross(glm::normalize(dir + glm::vec3(0, 0.001f, 0)),
                     glm::vec3(0, 1, 0));
      v.angVel = spinAxis * std::min(power * 0.12f, 5.0f);
    }
  }
  for (Prop& pr : props) {
    if (!pr.alive || pr.held) continue;
    glm::vec3 d = pr.pos - at;
    float dist = glm::length(d);
    if (dist > R) continue;
    if (pr.type == PropType::Tree && !pr.felled) {
      // Scorched in place: the fell-to-stump slot mutation, minus the logs -
      // fire gives nothing back.
      pr.type = PropType::Stump;
      pr.resource = 0.0f;
      pr.claimedBy = -1;
      pr.radius = 0.5f * pr.scale;
      pr.pos.y = restHeight(pr);
      pr.vel = glm::vec3(0.0f);
      pr.angVel = glm::vec3(0.0f);
      pr.asleep = true;
      pr.uprighting = false;
      continue;
    }
    glm::vec3 dir = dist > 0.01f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
    dir.y += 0.7f;
    dir = glm::normalize(dir);
    pr.vel += dir * (tune::kFireballImpulse * (1.0f - dist / R) * 0.8f);
    pr.asleep = false;
  }
  notifyDivineEvent(f.god, at, tune::kFearFireball, tune::kAweFireball);
  events.push_back({WorldEvent::Kind::Explosion, at, R, f.god});
  events.push_back({WorldEvent::Kind::Scream, at, 2.0f, f.god});
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
    bool onTempleGround = false;
    for (const God& g : gods)
      onTempleGround |=
          g.temple.founded &&
          glm::distance(glm::vec2(x, z),
                        glm::vec2(g.temple.pos.x, g.temple.pos.z)) < 18.0f;
    if (onTempleGround) continue;
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
    bool onTempleGround = false;
    for (const God& g : gods)
      onTempleGround |=
          g.temple.founded &&
          glm::distance(glm::vec2(x, z),
                        glm::vec2(g.temple.pos.x, g.temple.pos.z)) < 18.0f;
    if (onTempleGround) continue;

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
  // The app drains `events` after every update; headless runs never do, so
  // keep the tail bounded (they are flashes, not history).
  if (events.size() > 256) events.erase(events.begin(), events.end() - 64);
  dayCycle.advance(dt);
  rebuildObstacleGrid();

  // M11: showers age out; comets fall and burst on whatever they meet.
  for (RainCloud& r : rains) r.age += dt;
  std::erase_if(rains, [](const RainCloud& r) { return r.age >= r.duration; });
  for (std::size_t i = 0; i < fireballs.size();) {
    Fireball& f = fireballs[i];
    f.age += dt;
    f.vel.y -= kGravity * dt;
    f.pos += f.vel * dt;
    float ground = std::max(terrain.heightAt(f.pos.x, f.pos.z),
                            Terrain::WATER_LEVEL);
    if (f.pos.y <= ground + 0.4f || f.age > 8.0f) {
      Fireball burst = f;
      fireballs.erase(fireballs.begin() + static_cast<std::ptrdiff_t>(i));
      explodeFireball(burst);
    } else {
      ++i;
    }
  }
  // AI gods act here, in god-index order, before the villagers think - a
  // fixed spot in the frame so runs stay deterministic.
  for (int g = 0; g < tune::kMaxGods; ++g)
    if (gods[g].active && gods[g].ai) ai[g].update(*this, dt);
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

    float fallSpeed = -p.vel.y;
    p.pos += p.vel * dt;
    if (!inWater && fallSpeed > 2.5f &&
        p.pos.y - p.radius * 0.5f < Terrain::WATER_LEVEL)
      events.push_back({WorldEvent::Kind::Splash, p.pos, fallSpeed, -1});

    integrateTumble(p.rot, p.angVel, dt);

    // Terrain collision (single sphere) - shared with thrown villagers.
    float restitution = p.type == PropType::Rock ? 0.32f : 0.10f;
    bool onGround = false;
    float impact =
        collideSphereTerrain(p.pos, p.vel, p.angVel, p.radius, 0.55f, terrain,
                             restitution, 0.72f, 1.2f, onGround);
    if (onGround && impact > 5.0f)
      events.push_back({WorldEvent::Kind::Thud, p.pos, impact, -1});

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
            int sender = p.thrownByGod;
            v.absorbProp(*this, static_cast<int>(idx));
            if (!p.alive && sender >= 0)  // a skill-shot gift, received
              notifyDivineEvent(sender, p.pos, 0.0f, tune::kAweGift);
            absorbed = !p.alive;  // a full pile declines: fall through
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

  // One shared pass tallies rotting-body belief pressure per village
  // (bodies are rare; villages used to each scan every prop).
  corpseRot_.assign(villages.size(), 0.0f);
  for (const Prop& p : props) {
    if (!p.alive || p.type != PropType::Body || p.carrier >= 0 || p.held)
      continue;
    if (p.age <= tune::kCorpseRotDays * dayCycle.secondsPerDay) continue;
    for (std::size_t v = 0; v < villages.size(); ++v)
      if (villages[v].founded &&
          glm::distance(glm::vec2(p.pos.x, p.pos.z),
                        glm::vec2(villages[v].center.x, villages[v].center.z)) <
              60.0f)
        corpseRot_[v] += tune::kCorpseBeliefPerDay;
  }
  for (std::size_t v = 0; v < villages.size(); ++v)
    if (villages[v].founded) villages[v].step(*this, dt, corpseRot_[v]);
  updateOwnership();
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
  // Afloat, a stack rests on the water, never on the seabed below it.
  target.pos.y = std::max(restHeight(target),
                          Terrain::WATER_LEVEL + target.radius * 0.3f);
  held.alive = false;
  held.held = false;
  events.push_back({WorldEvent::Kind::Clack, target.pos, target.resource, -1});
  return best;
}

// Which village OWNED BY `god` would host a stack placed at `pos`? Runs
// every validity rule; returns -1 if the placement is invalid everywhere.
int World::scaffoldHostVillage(const glm::vec3& pos, int count, int god) const {
  glm::vec2 p2(pos.x, pos.z);

  // Center upgrades happen AT a totem; everything else needs open ground.
  bool isCenter =
      Village::buildingForStack(count, BuildingType::Store) == BuildingType::Center;
  for (std::size_t vi = 0; vi < villages.size(); ++vi) {
    const Village& v = villages[vi];
    if (!v.founded || v.owner != god) continue;
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
    for (const God& g : gods) {
      if (!g.temple.founded) continue;
      if (glm::distance(glm::vec2(g.temple.pos.x, g.temple.pos.z), p2) < 14.0f)
        return -1;
    }
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

bool World::scaffoldPlacementValid(const glm::vec3& pos, int count, int god) const {
  return scaffoldHostVillage(pos, count, god) >= 0;
}

bool World::tryPlaceScaffold(int scaffoldIdx, BuildingType civicChoice, int god) {
  if (scaffoldIdx < 0 || scaffoldIdx >= static_cast<int>(props.size())) return false;
  Prop& s = props[scaffoldIdx];
  if (!s.alive || s.type != PropType::Scaffold) return false;
  int count = std::clamp(static_cast<int>(std::lround(s.resource)), 1,
                         tune::kMaxScaffoldStack);
  int host = scaffoldHostVillage(s.pos, count, god);
  if (host < 0) return false;
  Village& village = villages[host];

  BuildingType type = Village::buildingForStack(count, civicChoice);
  if (type == BuildingType::Center) {
    // Upgrade the totem in place.
    Building& c = village.buildings[village.centerIdx];
    c.level = std::min(3, c.level + 1);
    s.alive = false;
    s.held = false;
    events.push_back({WorldEvent::Kind::Clack, c.pos, 2.0f, god});
    return true;
  }

  Building b;
  b.type = type;
  b.pos = glm::vec3(s.pos.x, terrain.heightAt(s.pos.x, s.pos.z), s.pos.z);
  glm::vec2 toCenter = glm::vec2(village.center.x, village.center.z) -
                       glm::vec2(s.pos.x, s.pos.z);
  b.yaw = noise::atan2det(toCenter.x, toCenter.y);
  b.stage = 0;
  b.tier = count;      // scaffolds ARE the material: no wood hauling
  b.woodCost = 0;
  village.buildings.push_back(b);
  s.alive = false;
  s.held = false;
  events.push_back({WorldEvent::Kind::Clack, b.pos, 2.0f, god});
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
