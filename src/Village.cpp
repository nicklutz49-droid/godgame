#include "Village.h"

#include <algorithm>
#include <cmath>

#include "Noise.h"
#include "Tuning.h"
#include "World.h"

using noise::XorShift;

namespace {

// Hardcoded direction table instead of sin/cos so village founding stays
// bit-identical across platforms (same reason terrain gen only uses noise::*).
const glm::vec2 kDirs8[8] = {
    {1.0f, 0.0f},   {0.70711f, 0.70711f},   {0.0f, 1.0f},   {-0.70711f, 0.70711f},
    {-1.0f, 0.0f},  {-0.70711f, -0.70711f}, {0.0f, -1.0f},  {0.70711f, -0.70711f}};

glm::vec2 xz(const glm::vec3& p) { return {p.x, p.z}; }

}  // namespace

void Village::plan(World& world, std::uint32_t seed) {
  Terrain& terrain = world.terrain;
  rng_ = seed * 2654435761u + 97u;

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
    float score = -1.0e9f;
    float x = 0.0f, z = 0.0f;
  };
  auto scan = [&](float minFlat, float maxCoast) {
    Candidate best;
    const float lim = Terrain::SIZE * 0.42f;
    for (float z = -lim; z <= lim; z += 8.0f) {
      for (float x = -lim; x <= lim; x += 8.0f) {
        float h = terrain.heightAt(x, z);
        if (h < 2.5f || h > 14.0f) continue;
        float flat = flatnessAt(x, z);
        if (flat < minFlat) continue;
        float coast = coastDist(x, z);
        if (coast > maxCoast) continue;
        float forest = noise::fbm(x * 0.016f, z * 0.016f, 3, seed + 31u);
        float score = flat * 3.0f + forest * 1.2f + (1.0f - coast / maxCoast) -
                      std::abs(h - 5.0f) * 0.08f;
        if (score > best.score) best = {score, x, z};
      }
    }
    return best;
  };

  // Three-pass hostile-island policy: strict -> relaxed -> terraform harder.
  // Never regenerate: "seed 1234" must stay this island.
  Candidate site = scan(0.93f, 80.0f);
  bool terraformHard = false;
  if (site.score < -1.0e8f) site = scan(0.86f, 130.0f);
  if (site.score < -1.0e8f) {
    terraformHard = true;
    const float lim = Terrain::SIZE * 0.42f;
    for (float z = -lim; z <= lim; z += 8.0f) {
      for (float x = -lim; x <= lim; x += 8.0f) {
        float h = terrain.heightAt(x, z);
        float score = (h > 0.5f ? 5.0f - std::abs(h - 6.0f) * 0.3f : h) +
                      flatnessAt(x, z) * 2.0f;
        if (score > site.score) site = {score, x, z};
      }
    }
  }

  float targetH = std::clamp(terrain.heightAt(site.x, site.z), 2.5f, 12.0f);
  terrain.flattenDisc(site.x, site.z, radius, targetH, terraformHard ? 1.0f : 0.88f);
  // The field sits at the terrace edge - level its rectangle too, before any
  // building height is snapped.
  glm::vec2 fieldC = glm::vec2(site.x, site.z) + kDirs8[2] * 20.0f;
  terrain.flattenDisc(fieldC.x, fieldC.y, 18.0f, targetH, 1.0f);
  center = glm::vec3(site.x, terrain.heightAt(site.x, site.z), site.z);
  founded = true;

  // --- layout on the terrace (positions snapped to the flattened ground) ---
  XorShift rng(seed * 1000003u + 7u);
  auto place = [&](BuildingType type, const glm::vec2& dir, float dist, int stage,
                   int woodCost) {
    Building b;
    b.type = type;
    glm::vec2 p = xz(center) + dir * dist;
    b.pos = glm::vec3(p.x, terrain.heightAt(p.x, p.y), p.y);
    glm::vec2 toCenter = xz(center) - p;
    b.yaw = std::atan2(toCenter.x, toCenter.y);  // face the center
    b.stage = stage;
    b.woodCost = woodCost;
    buildings.push_back(b);
    return static_cast<int>(buildings.size() - 1);
  };

  centerIdx = place(BuildingType::Center, kDirs8[0], 0.0f, 3, 0);
  storageIdx = place(BuildingType::Storage, kDirs8[0], 9.0f, 3, 0);
  campfireIdx = place(BuildingType::Campfire, kDirs8[5], 6.5f, 3, 0);

  // Three finished houses, one open construction site, three reserved plots.
  place(BuildingType::House, kDirs8[1], 15.0f, 3, 0);
  place(BuildingType::House, kDirs8[3], 15.0f, 3, 0);
  place(BuildingType::House, kDirs8[4], 15.0f, 3, 0);
  place(BuildingType::House, kDirs8[6], 15.0f, 0, tune::kHouseWoodCost);
  place(BuildingType::House, kDirs8[7], 22.0f, -1, 0);
  place(BuildingType::House, kDirs8[0], 22.0f, -1, 0);
  place(BuildingType::House, kDirs8[5], 22.0f, -1, 0);

  // Field: a tilled rectangle on the terrace, 6x4 crop cells.
  fieldCenter = fieldC;
  fieldHalf = glm::vec2(8.0f, 5.0f);
  farmCells.clear();
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 6; ++i) {
      FarmCell c;
      c.pos = fieldCenter + glm::vec2((static_cast<float>(i) + 0.5f) / 6.0f - 0.5f,
                                      0.0f) *
                                (fieldHalf.x * 2.0f) +
              glm::vec2(0.0f, (static_cast<float>(j) + 0.5f) / 4.0f - 0.5f) *
                  (fieldHalf.y * 2.0f);
      c.growth = rng.range(0.15f, 0.85f);  // staggered so harvests trickle in
      farmCells.push_back(c);
    }
  }

  // Fishing spots: march each compass direction to the first waterline.
  fishingSpots.clear();
  for (const glm::vec2& dir : kDirs8) {
    if (fishingSpots.size() >= 3) break;
    for (float d = 10.0f; d <= 130.0f; d += 4.0f) {
      glm::vec2 p = xz(center) + dir * d;
      if (terrain.heightAt(p.x, p.y) < 0.1f) {
        glm::vec2 spot = xz(center) + dir * (d - 3.0f);
        bool farEnough = true;
        for (const glm::vec3& s : fishingSpots)
          if (glm::distance(glm::vec2(s.x, s.z), spot) < 15.0f) farEnough = false;
        if (farEnough)
          fishingSpots.push_back(
              glm::vec3(spot.x, terrain.heightAt(spot.x, spot.y), spot.y));
        break;
      }
    }
  }

  wood = tune::kStartWood;
  food = tune::kStartFood;
}

void Village::spawnVillagers(World& world, std::uint32_t seed) {
  villagers.clear();
  if (!founded) return;
  XorShift rng(seed ^ 0xC0FFEE11u);
  const Job starterJobs[8] = {Job::Forester, Job::Farmer, Job::Fisherman,
                              Job::Builder,  Job::None,   Job::None,
                              Job::None,     Job::None};
  glm::vec3 fire = campfirePos();
  for (int i = 0; i < tune::kStartPopulation; ++i) {
    Villager v;
    v.rng = seed * 1000003u + static_cast<std::uint32_t>(i) * 2654435761u + 1u;
    glm::vec2 p = xz(fire) + kDirs8[i % 8] * rng.range(3.0f, 6.5f);
    v.pos = glm::vec3(p.x, world.terrain.heightAt(p.x, p.y), p.y);
    v.yaw = rng.range(0.0f, 6.2831f);
    v.job = starterJobs[i % 8];
    v.variant = static_cast<int>(rng.next() % 3u);
    v.hunger = rng.range(0.1f, 0.45f);
    v.thinkTimer = tune::kThinkInterval * (static_cast<float>(i) + 1.0f) /
                   static_cast<float>(tune::kStartPopulation);
    villagers.push_back(v);
    villagers.back().home = findHomeFor(i);
  }
}

void Village::step(World& world, float dt) {
  if (!founded) return;
  float dayFrac = dt / world.dayCycle.secondsPerDay;
  float sun = 0.25f + 0.75f * world.dayCycle.daylight();  // crops rest at night

  for (FarmCell& c : farmCells) {
    c.tendedTimer = std::max(0.0f, c.tendedTimer - dt);
    float rate = tune::kCropGrowPerDay * (c.tendedTimer > 0.0f ? tune::kCropTendBoost : 1.0f);
    c.growth = std::min(1.0f, c.growth + rate * sun * dayFrac);
  }

  // Open a new construction site when housing gets tight and wood exists.
  bool hasOpenSite = false;
  for (const Building& b : buildings)
    if (b.type == BuildingType::House && b.stage >= 0 && b.stage < 3) hasOpenSite = true;
  if (!hasOpenSite && population() >= static_cast<int>(0.8f * housingCapacity()) &&
      wood >= tune::kHouseWoodCost) {
    for (Building& b : buildings) {
      if (b.type == BuildingType::House && b.stage == -1) {
        b.stage = 0;
        b.woodCost = tune::kHouseWoodCost;
        b.woodDelivered = 0;
        b.buildProgress = 0.0f;
        break;
      }
    }
  }

  // Dawn tick: modest population growth. (Belief will later feed a happiness
  // multiplier into exactly this check.)
  float t = world.dayCycle.t;
  if (lastT >= 0.0f && lastT < tune::kDawnT && t >= tune::kDawnT) {
    bool surplus = food > static_cast<int>(tune::kGrowthFoodPerCapita *
                                           static_cast<float>(population()));
    if (surplus && population() < housingCapacity() &&
        population() < tune::kMaxPopulation) {
      XorShift rng(rng_);
      // A child appears at an occupied finished house.
      std::vector<int> occupied;
      for (std::size_t b = 0; b < buildings.size(); ++b)
        if (buildings[b].type == BuildingType::House && buildings[b].stage == 3 &&
            buildings[b].residents > 0)
          occupied.push_back(static_cast<int>(b));
      if (!occupied.empty()) {
        int house = occupied[rng.next() % occupied.size()];
        Villager child;
        child.scale = tune::kChildScale;
        child.rng = rng.next();
        glm::vec3 f(std::sin(buildings[house].yaw), 0.0f, std::cos(buildings[house].yaw));
        child.pos = buildings[house].pos + f * 3.0f;
        child.pos.y = world.terrain.heightAt(child.pos.x, child.pos.z);
        child.yaw = buildings[house].yaw;
        child.variant = static_cast<int>(rng.next() % 3u);
        child.hunger = 0.2f;
        child.thinkTimer = 0.2f;
        villagers.push_back(child);
        villagers.back().home = findHomeFor(static_cast<int>(villagers.size()) - 1);
      }
      rng_ = rng.state;
    }
  }
  lastT = t;
}

Job Village::resolveJobAtPoint(const World& world, const glm::vec3& p) const {
  // Priority: construction site > field > tree > water/shore.
  for (const Building& b : buildings)
    if (b.type == BuildingType::House && b.stage >= 0 && b.stage < 3 &&
        glm::distance(xz(b.pos), xz(p)) < 6.0f)
      return Job::Builder;

  if (std::abs(p.x - fieldCenter.x) < fieldHalf.x + 2.0f &&
      std::abs(p.z - fieldCenter.y) < fieldHalf.y + 2.0f)
    return Job::Farmer;

  for (const Prop& prop : world.props)
    if (prop.alive && prop.type == PropType::Tree && prop.resource > 0.0f &&
        !prop.felled && glm::distance(xz(prop.pos), xz(p)) < 4.0f)
      return Job::Forester;

  if (world.terrain.heightAt(p.x, p.z) < Terrain::WATER_LEVEL + 0.05f)
    return Job::Fisherman;
  for (const glm::vec2& dir : kDirs8)
    if (world.terrain.heightAt(p.x + dir.x * 4.0f, p.z + dir.y * 4.0f) <
        Terrain::WATER_LEVEL)
      return Job::Fisherman;

  return Job::None;
}

bool Village::inStorageRadius(const glm::vec3& p) const {
  if (storageIdx < 0) return false;
  return glm::distance(xz(buildings[storageIdx].pos), xz(p)) < 5.5f;
}

bool Village::insideFootprint(float x, float z) const {
  if (!founded) return false;
  if (glm::distance(glm::vec2(x, z), xz(center)) < radius + 4.0f) return true;
  return std::abs(x - fieldCenter.x) < fieldHalf.x + 3.0f &&
         std::abs(z - fieldCenter.y) < fieldHalf.y + 3.0f;
}

int Village::housingCapacity() const {
  int cap = 0;
  for (const Building& b : buildings)
    if (b.type == BuildingType::House && b.stage == 3) cap += 4;
  return cap;
}

void Village::absorbProp(World& world, int propIdx) {
  Prop& p = world.props[propIdx];
  if (!p.alive) return;
  switch (p.type) {
    case PropType::Log:
      wood += 1;
      woodProduced += 1;
      break;
    case PropType::Tree:
      wood += std::max(1, static_cast<int>(std::lround(
                              tune::kLogsPerTree * p.scale)));
      woodProduced += std::max(1, static_cast<int>(std::lround(
                                      tune::kLogsPerTree * p.scale)));
      break;
    case PropType::Food:
      food += std::max(1, static_cast<int>(std::lround(p.resource)));
      foodProduced += std::max(1, static_cast<int>(std::lround(p.resource)));
      break;
    default:
      return;  // rocks and stumps are not storeable
  }
  p.alive = false;
  p.held = false;
  p.carrier = -1;
  p.claimedBy = -1;
}

void Village::notifyDivineEvent(const glm::vec3& where, float magnitude) {
  // Fear ripples out to witnesses. (The belief slice will ride this same bus:
  // miracles witnessed, gifts received - all divine acts funnel through here.)
  for (Villager& v : villagers) {
    if (v.inside) continue;
    float d = glm::distance(xz(v.pos), xz(where));
    if (d < 30.0f)
      v.fear = std::min(1.0f, v.fear + magnitude * (1.0f - d / 30.0f));
  }
}

glm::vec3 Village::storagePos() const {
  return storageIdx >= 0 ? buildings[storageIdx].pos : center;
}

glm::vec3 Village::campfirePos() const {
  return campfireIdx >= 0 ? buildings[campfireIdx].pos : center;
}

int Village::findHomeFor(int villagerIdx) {
  (void)villagerIdx;
  for (std::size_t b = 0; b < buildings.size(); ++b) {
    Building& h = buildings[b];
    if (h.type == BuildingType::House && h.stage == 3 && h.residents < 4) {
      ++h.residents;
      return static_cast<int>(b);
    }
  }
  return -1;
}

void Village::onVillagerDeath(int villagerIdx) {
  // MORTALITY SEAM (unreachable while tune::kVillagersInvulnerable): free the
  // bed; the mortality slice adds body props, graves, and a belief penalty.
  Villager& v = villagers[villagerIdx];
  if (v.home >= 0 && buildings[v.home].residents > 0) --buildings[v.home].residents;
  v.home = -1;
}
