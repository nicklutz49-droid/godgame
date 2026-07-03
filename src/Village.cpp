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

void Village::addField(glm::vec2 center2, glm::vec2 half, noise::XorShift* rng) {
  fields.push_back({center2, half});
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 6; ++i) {
      FarmCell c;
      c.pos = center2 +
              glm::vec2((static_cast<float>(i) + 0.5f) / 6.0f - 0.5f, 0.0f) *
                  (half.x * 2.0f) +
              glm::vec2(0.0f, (static_cast<float>(j) + 0.5f) / 4.0f - 0.5f) *
                  (half.y * 2.0f);
      c.growth = rng ? rng->range(0.15f, 0.85f) : 0.1f;
      farmCells.push_back(c);
    }
  }
}

void Village::plan(World& world, std::uint32_t seed, glm::vec2 site,
                   bool terraformHard) {
  Terrain& terrain = world.terrain;
  rng_ = seed * 2654435761u + 97u;

  float targetH = std::clamp(terrain.heightAt(site.x, site.y), 2.5f, 12.0f);
  terrain.flattenDisc(site.x, site.y, radius, targetH, terraformHard ? 1.0f : 0.88f);
  // The field sits at the terrace edge - level its rectangle too, before any
  // building height is snapped.
  glm::vec2 fieldC = site + kDirs8[2] * 20.0f;
  terrain.flattenDisc(fieldC.x, fieldC.y, 18.0f, targetH, 1.0f);
  center = glm::vec3(site.x, terrain.heightAt(site.x, site.y), site.y);
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

  // Three finished houses, a workshop (the scaffold engine needs a bootstrap),
  // one open construction site, three reserved plots.
  place(BuildingType::House, kDirs8[1], 15.0f, 3, 0);
  place(BuildingType::House, kDirs8[3], 15.0f, 3, 0);
  place(BuildingType::House, kDirs8[4], 15.0f, 3, 0);
  place(BuildingType::Workshop, kDirs8[3], 24.0f, 3, 0);
  place(BuildingType::House, kDirs8[6], 15.0f, 0, tune::kHouseWoodCost);
  place(BuildingType::House, kDirs8[7], 22.0f, -1, 0);
  place(BuildingType::House, kDirs8[0], 22.0f, -1, 0);
  place(BuildingType::House, kDirs8[5], 22.0f, -1, 0);

  // Founding field: a tilled rectangle on the terrace, 6x4 crop cells.
  fields.clear();
  farmCells.clear();
  addField(fieldC, glm::vec2(8.0f, 5.0f), &rng);

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
  belief = owner == 0 ? tune::kBeliefStart : tune::kNeutralBeliefStart;
}

void Village::spawnVillagers(World& world, std::uint32_t seed, int villageIdx) {
  villagers.clear();
  if (!founded) return;
  XorShift rng(seed ^ (0xC0FFEE11u + static_cast<std::uint32_t>(villageIdx) * 7919u));
  // Neutral villages spawn no Worshipper - they have no god to dance for.
  const Job ownedJobs[8] = {Job::Forester, Job::Farmer,     Job::Fisherman,
                            Job::Builder,  Job::Worshipper, Job::None,
                            Job::None,     Job::None};
  const Job neutralJobs[8] = {Job::Forester, Job::Farmer, Job::Fisherman,
                              Job::Builder,  Job::None,   Job::None,
                              Job::None,     Job::None};
  const Job* starterJobs = owner == 0 ? ownedJobs : neutralJobs;
  glm::vec3 fire = campfirePos();
  for (int i = 0; i < tune::kStartPopulation; ++i) {
    Villager v;
    v.rng = seed * 1000003u +
            static_cast<std::uint32_t>(i + villageIdx * 131) * 2654435761u + 1u;
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

  // Faith fades unless the god stays present (worship counteracts this).
  // A Wonder slows the fade; a tended Graveyard quietly sustains it; a body
  // left rotting near the village is an accusation nobody forgets.
  float decay = tune::kBeliefDecayPerDay;
  if (insideWonderAura(center)) decay *= tune::kWonderDecayFactor;
  decay -= static_cast<float>(countCompleted(BuildingType::Graveyard)) *
           tune::kGraveyardBeliefPerDay;
  for (const Prop& p : world.props) {
    if (!p.alive || p.type != PropType::Body || p.carrier >= 0 || p.held) continue;
    if (p.age > tune::kCorpseRotDays * world.dayCycle.secondsPerDay &&
        glm::distance(xz(p.pos), xz(center)) < 60.0f)
      decay += tune::kCorpseBeliefPerDay;
  }
  float floor = owner == 0 ? tune::kBeliefFloor : tune::kNeutralBeliefFloor;
  belief = std::max(floor, belief - decay * dayFrac);

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

  // Dawn tick: modest population growth. A Crèche eases the surplus needed
  // and hosts the newborns. (Belief will later feed a happiness multiplier
  // into exactly this check.)
  float t = world.dayCycle.t;
  if (lastT >= 0.0f && lastT < tune::kDawnT && t >= tune::kDawnT) {
    float needed = tune::kGrowthFoodPerCapita * static_cast<float>(population());
    int creches = countCompleted(BuildingType::Creche);
    if (creches > 0) needed *= tune::kCrecheSurplusFactor;
    bool surplus = food > static_cast<int>(needed);
    if (surplus && population() < housingCapacity() &&
        population() < tune::kMaxPopulation) {
      XorShift rng(rng_);
      // A child appears at the creche if there is one, else at a home.
      std::vector<int> occupied;
      for (std::size_t b = 0; b < buildings.size(); ++b) {
        if (buildings[b].stage != 3) continue;
        if (buildings[b].type == BuildingType::Creche)
          occupied.push_back(static_cast<int>(b));
      }
      if (occupied.empty()) {
        for (std::size_t b = 0; b < buildings.size(); ++b)
          if (buildings[b].type == BuildingType::House && buildings[b].stage == 3 &&
              buildings[b].residents > 0)
            occupied.push_back(static_cast<int>(b));
      }
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
  // Priority: totem > construction site > field > tree > water/shore.
  if (centerIdx >= 0 &&
      glm::distance(xz(buildings[centerIdx].pos), xz(p)) < 5.0f)
    return Job::Worshipper;

  for (const Building& b : buildings)
    if (b.stage >= 0 && b.stage < 3 && glm::distance(xz(b.pos), xz(p)) < 6.0f)
      return Job::Builder;

  if (insideAnyField(p.x, p.z, 2.0f)) return Job::Farmer;

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
  return insideAnyField(x, z, 3.0f);
}

bool Village::insideAnyField(float x, float z, float margin) const {
  for (const Field& f : fields)
    if (std::abs(x - f.center.x) < f.half.x + margin &&
        std::abs(z - f.center.y) < f.half.y + margin)
      return true;
  return false;
}

int Village::population() const {
  int n = 0;
  for (const Villager& v : villagers)
    if (v.alive) ++n;
  return n;
}

int Village::completedGraveyard() const {
  for (std::size_t b = 0; b < buildings.size(); ++b)
    if (buildings[b].type == BuildingType::Graveyard && buildings[b].stage == 3)
      return static_cast<int>(b);
  return -1;
}

bool Village::buryBody(World& world, int propIdx) {
  Prop& body = world.props[propIdx];
  if (!body.alive || body.type != PropType::Body) return false;
  int g = completedGraveyard();
  if (g < 0) return false;
  if (glm::distance(xz(buildings[g].pos), xz(body.pos)) > 6.0f) return false;
  body.alive = false;
  body.held = false;
  body.carrier = -1;
  body.claimedBy = -1;
  ++buildings[g].charges;  // a fresh grave (rendered as headstones)
  ++burials;
  belief = std::min(1.0f, belief + tune::kBurialBelief);  // dignity matters
  return true;
}

int Village::housingCapacity() const {
  int cap = 0;
  for (const Building& b : buildings) {
    if (b.stage != 3) continue;
    if (b.type == BuildingType::House) cap += tune::kBedsSmallAbode;
    if (b.type == BuildingType::LargeAbode) cap += tune::kBedsLargeAbode;
  }
  return cap;
}

int Village::foodCap() const {
  return tune::kBaseFoodCap +
         countCompleted(BuildingType::Store) * tune::kStoreFoodCap;
}

int Village::woodCap() const {
  return tune::kBaseWoodCap +
         countCompleted(BuildingType::Store) * tune::kStoreWoodCap;
}

int Village::countCompleted(BuildingType type) const {
  int n = 0;
  for (const Building& b : buildings)
    if (b.type == type && b.stage == 3) ++n;
  return n;
}

BuildingType Village::buildingForStack(int count, BuildingType civicChoice) {
  switch (count) {
    case 1: return BuildingType::House;
    case 2: return BuildingType::LargeAbode;
    case 3:
      // The wheel picks among the civic four; anything else defaults to Store.
      if (civicChoice == BuildingType::Workshop || civicChoice == BuildingType::Creche ||
          civicChoice == BuildingType::Graveyard || civicChoice == BuildingType::Store)
        return civicChoice;
      return BuildingType::Store;
    case 4: return BuildingType::FieldSite;
    case 5: return BuildingType::Center;  // upgrade at the totem
    case 6: return BuildingType::Dispenser;
    default: return BuildingType::Wonder;  // 7
  }
}

void Village::onBuildingComplete(World& world, int buildingIdx) {
  Building& b = buildings[buildingIdx];
  b.stage = 3;
  switch (b.type) {
    case BuildingType::House:
    case BuildingType::LargeAbode:
      // Home the (living) homeless in the new beds.
      for (std::size_t o = 0; o < villagers.size(); ++o)
        if (villagers[o].alive && villagers[o].home < 0)
          villagers[o].home = findHomeFor(static_cast<int>(o));
      break;
    case BuildingType::FieldSite: {
      addField(xz(b.pos), glm::vec2(8.0f, 5.0f), nullptr);
      break;
    }
    default:
      break;  // Store/Workshop/Creche/Graveyard/Dispenser/Wonder act via queries
  }
  (void)world;
}

float Village::centerManaMultiplier() const {
  float lvl = centerLevel();
  return 1.0f + tune::kCenterManaPerLevel * (lvl - 1.0f);
}

float Village::centerLevel() const {
  return centerIdx >= 0 ? static_cast<float>(buildings[centerIdx].level) : 1.0f;
}

bool Village::insideWonderAura(const glm::vec3& p) const {
  for (const Building& b : buildings)
    if (b.type == BuildingType::Wonder && b.stage == 3 &&
        glm::distance(xz(b.pos), xz(p)) < tune::kWonderAuraRadius)
      return true;
  return false;
}

void Village::absorbProp(World& world, int propIdx) {
  Prop& p = world.props[propIdx];
  if (!p.alive) return;
  switch (p.type) {
    case PropType::Log: {
      if (wood >= woodCap()) return;  // pile is full; the log stays put
      wood += 1;
      woodProduced += 1;
      break;
    }
    case PropType::Tree: {
      if (wood >= woodCap()) return;
      int value = std::max(1, static_cast<int>(std::lround(
                                  tune::kLogsPerTree * p.scale)));
      value = std::min(value, woodCap() - wood);
      wood += value;
      woodProduced += value;
      break;
    }
    case PropType::Food: {
      if (food >= foodCap()) return;
      int value = std::max(1, static_cast<int>(std::lround(p.resource)));
      value = std::min(value, foodCap() - food);
      food += value;
      foodProduced += value;
      break;
    }
    default:
      return;  // rocks, stumps and scaffolds are not storeable
  }
  p.alive = false;
  p.held = false;
  p.carrier = -1;
  p.claimedBy = -1;
}

void Village::notifyDivineEvent(const glm::vec3& where, float fear, float awe) {
  // Every divine act funnels through here: fear ripples out to individual
  // witnesses, and belief rises by how much of the village saw it.
  int witnesses = 0;
  for (Villager& v : villagers) {
    if (v.inside || !v.alive) continue;
    float d = glm::distance(xz(v.pos), xz(where));
    if (d < 30.0f) {
      ++witnesses;
      if (fear > 0.0f)
        v.fear = std::min(1.0f, v.fear + fear * (1.0f - d / 30.0f));
    }
  }
  if (awe > 0.0f && population() > 0) {
    if (insideWonderAura(where)) awe *= tune::kWonderAweFactor;
    belief = std::min(1.0f, belief + awe * static_cast<float>(witnesses) /
                                        static_cast<float>(population()));
  }
}

float Village::influenceRadius() const {
  return tune::kVillageInfluenceBase + belief * tune::kVillageInfluenceScale +
         (centerLevel() - 1.0f) * tune::kCenterInfluencePerLevel;
}

int Village::activeWorshippers() const {
  int n = 0;
  for (const Villager& v : villagers)
    if (v.alive && v.job == Job::Worshipper && v.state == VState::Work && !v.inside)
      ++n;
  return n;
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
