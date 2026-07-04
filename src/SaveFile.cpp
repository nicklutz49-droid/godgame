#include "SaveFile.h"

#include <cstdio>

#include "Serial.h"
#include "Tuning.h"
#include "Villagers.h"
#include "World.h"

// Friend of World / Village / GodAI: the one place allowed to restore their
// private state (founding rng streams, timers) field-for-field.
struct SaveIO {
  static void putProp(serial::Writer& w, const Prop& p) {
    w.u8(static_cast<std::uint8_t>(p.type));
    w.i32(p.variant);
    w.v3(p.pos);
    w.v3(p.vel);
    w.quat(p.rot);
    w.v3(p.angVel);
    w.f32(p.scale);
    w.f32(p.radius);
    w.f32(p.baseYaw);
    w.b(p.alive);
    w.b(p.asleep);
    w.b(p.uprighting);
    w.b(p.felled);
    w.f32(p.restTimer);
    w.f32(p.resource);
    w.f32(p.age);
    w.i32(p.claimedBy);
    w.i32(p.carrier);
    w.i32(p.thrownByGod);
  }

  static void getProp(serial::Reader& r, Prop& p) {
    p.type = static_cast<PropType>(r.u8());
    p.variant = r.i32();
    p.pos = r.v3();
    p.vel = r.v3();
    p.rot = r.quat();
    p.angVel = r.v3();
    p.scale = r.f32();
    p.radius = r.f32();
    p.baseYaw = r.f32();
    p.alive = r.b();
    p.asleep = r.b();
    p.uprighting = r.b();
    p.felled = r.b();
    p.restTimer = r.f32();
    p.resource = r.f32();
    p.age = r.f32();
    p.claimedBy = r.i32();
    p.carrier = r.i32();
    p.thrownByGod = r.i32();
    p.held = false;  // hands were settled before the save
  }

  static void putVillager(serial::Writer& w, const Villager& v) {
    w.v3(v.pos);
    w.v3(v.vel);
    w.quat(v.rot);
    w.v3(v.angVel);
    w.f32(v.yaw);
    w.f32(v.scale);
    w.u8(static_cast<std::uint8_t>(v.job));
    w.u8(static_cast<std::uint8_t>(v.state));
    w.f32(v.stateTimer);
    w.f32(v.thinkTimer);
    w.f32(v.hunger);
    w.f32(v.energy);
    w.f32(v.fear);
    w.f32(v.walkPhase);
    w.f32(v.workTimer);
    w.i32(v.workCount);
    w.v2(v.moveTarget);
    w.i32(v.targetProp);
    w.i32(v.blacklistProp);
    w.f32(v.blacklistTimer);
    w.i32(v.carriedProp);
    w.i32(v.targetBuilding);
    w.i32(v.targetCell);
    w.i32(v.home);
    w.b(v.alive);
    w.b(v.inside);
    w.b(v.pendingAssign);
    w.b(v.wasThrown);
    w.f32(v.stun);
    w.f32(v.submergedTime);
    w.f32(v.starveTimer);
    w.f32(v.progressTimer);
    w.v2(v.lastProgressPos);
    w.u32(v.rng);
    w.i32(v.variant);
    w.f32(v.headLook);
    w.f32(v.assignedFlash);
    w.f32(v.danceAngle);
  }

  static void getVillager(serial::Reader& r, Villager& v) {
    v.pos = r.v3();
    v.vel = r.v3();
    v.rot = r.quat();
    v.angVel = r.v3();
    v.yaw = r.f32();
    v.scale = r.f32();
    v.job = static_cast<Job>(r.u8());
    v.state = static_cast<VState>(r.u8());
    v.stateTimer = r.f32();
    v.thinkTimer = r.f32();
    v.hunger = r.f32();
    v.energy = r.f32();
    v.fear = r.f32();
    v.walkPhase = r.f32();
    v.workTimer = r.f32();
    v.workCount = r.i32();
    v.moveTarget = r.v2();
    v.targetProp = r.i32();
    v.blacklistProp = r.i32();
    v.blacklistTimer = r.f32();
    v.carriedProp = r.i32();
    v.targetBuilding = r.i32();
    v.targetCell = r.i32();
    v.home = r.i32();
    v.alive = r.b();
    v.inside = r.b();
    v.pendingAssign = r.b();
    v.wasThrown = r.b();
    v.stun = r.f32();
    v.submergedTime = r.f32();
    v.starveTimer = r.f32();
    v.progressTimer = r.f32();
    v.lastProgressPos = r.v2();
    v.rng = r.u32();
    v.variant = r.i32();
    v.headLook = r.f32();
    v.assignedFlash = r.f32();
    v.danceAngle = r.f32();
    v.held = false;       // hands were settled before the save
    v.lookAtHand = false; // re-derived every frame
  }

  static void putVillage(serial::Writer& w, const Village& v) {
    w.b(v.founded);
    w.i32(v.owner);
    w.i32(v.startPreset);
    w.v3(v.center);
    w.f32(v.radius);
    for (int g = 0; g < tune::kMaxGods; ++g) w.f32(v.belief[g]);
    w.i32(v.wood);
    w.i32(v.food);
    w.i32(v.woodProduced);
    w.i32(v.foodProduced);
    w.i32(v.mealsEaten);
    w.i32(v.stuckEvents);
    w.i32(v.scaffoldsCrafted);
    w.i32(v.deaths);
    w.i32(v.burials);
    w.i32(v.centerIdx);
    w.i32(v.storageIdx);
    w.i32(v.campfireIdx);
    w.f32(v.dispenserFill);
    w.f32(v.lastT);
    w.u32(v.rng_);

    w.u32(static_cast<std::uint32_t>(v.buildings.size()));
    for (const Building& b : v.buildings) {
      w.u8(static_cast<std::uint8_t>(b.type));
      w.v3(b.pos);
      w.f32(b.yaw);
      w.i32(b.stage);
      w.i32(b.woodCost);
      w.i32(b.woodDelivered);
      w.f32(b.buildProgress);
      w.i32(b.residents);
      w.i32(b.tier);
      w.i32(b.level);
      w.i32(b.charges);
    }
    w.u32(static_cast<std::uint32_t>(v.fields.size()));
    for (const Field& f : v.fields) {
      w.v2(f.center);
      w.v2(f.half);
    }
    w.u32(static_cast<std::uint32_t>(v.farmCells.size()));
    for (const FarmCell& c : v.farmCells) {
      w.v2(c.pos);
      w.f32(c.growth);
      w.f32(c.tendedTimer);
      w.i32(c.claimedBy);
    }
    w.u32(static_cast<std::uint32_t>(v.fishingSpots.size()));
    for (const glm::vec3& s : v.fishingSpots) w.v3(s);
    w.u32(static_cast<std::uint32_t>(v.villagers.size()));
    for (const Villager& p : v.villagers) putVillager(w, p);
  }

  static bool getVillage(serial::Reader& r, Village& v) {
    v.founded = r.b();
    v.owner = r.i32();
    v.startPreset = r.i32();
    v.center = r.v3();
    v.radius = r.f32();
    for (int g = 0; g < tune::kMaxGods; ++g) v.belief[g] = r.f32();
    v.wood = r.i32();
    v.food = r.i32();
    v.woodProduced = r.i32();
    v.foodProduced = r.i32();
    v.mealsEaten = r.i32();
    v.stuckEvents = r.i32();
    v.scaffoldsCrafted = r.i32();
    v.deaths = r.i32();
    v.burials = r.i32();
    v.centerIdx = r.i32();
    v.storageIdx = r.i32();
    v.campfireIdx = r.i32();
    v.dispenserFill = r.f32();
    v.lastT = r.f32();
    v.rng_ = r.u32();

    std::uint32_t nb = r.u32();
    if (!r.ok || nb > 4096u) return false;
    v.buildings.assign(nb, Building{});
    for (Building& b : v.buildings) {
      b.type = static_cast<BuildingType>(r.u8());
      b.pos = r.v3();
      b.yaw = r.f32();
      b.stage = r.i32();
      b.woodCost = r.i32();
      b.woodDelivered = r.i32();
      b.buildProgress = r.f32();
      b.residents = r.i32();
      b.tier = r.i32();
      b.level = r.i32();
      b.charges = r.i32();
    }
    std::uint32_t nf = r.u32();
    if (!r.ok || nf > 4096u) return false;
    v.fields.assign(nf, Field{});
    for (Field& f : v.fields) {
      f.center = r.v2();
      f.half = r.v2();
    }
    std::uint32_t nc = r.u32();
    if (!r.ok || nc > 65536u) return false;
    v.farmCells.assign(nc, FarmCell{});
    for (FarmCell& c : v.farmCells) {
      c.pos = r.v2();
      c.growth = r.f32();
      c.tendedTimer = r.f32();
      c.claimedBy = r.i32();
    }
    std::uint32_t ns = r.u32();
    if (!r.ok || ns > 64u) return false;
    v.fishingSpots.assign(ns, glm::vec3(0.0f));
    for (glm::vec3& s : v.fishingSpots) s = r.v3();
    std::uint32_t nv = r.u32();
    if (!r.ok || nv > 4096u) return false;
    v.villagers.assign(nv, Villager{});
    for (Villager& p : v.villagers) getVillager(r, p);
    return r.ok;
  }

  static void save(World& world, std::vector<std::uint8_t>& out,
                   const savefile::CamState* cam) {
    // Settle every hand: a save never contains anything mid-grip.
    for (int g = 0; g < tune::kMaxGods; ++g)
      if (world.gods[g].active && world.gods[g].ai) world.ai[g].settle(world);
    for (Prop& p : world.props)
      if (p.held) {
        p.held = false;
        p.asleep = false;
      }
    for (Village& v : world.villages)
      for (Villager& p : v.villagers)
        if (p.held) {
          p.held = false;
          p.state = VState::Airborne;
          p.vel = glm::vec3(0.0f);
          p.pendingAssign = false;
        }

    out.clear();
    serial::Writer w{out};
    w.u8('G');
    w.u8('S');
    w.u8('A');
    w.u8('V');
    w.u32(savefile::kVersion);
    w.u32(world.seed_);
    w.u32(world.miracleCounter_);
    w.u32(world.editStroke_);
    w.f32(world.dayCycle.t);
    w.i32(world.dayCycle.day);
    w.f32(world.dayCycle.secondsPerDay);

    w.u32(static_cast<std::uint32_t>(Terrain::GRID + 1));
    for (float h : world.terrain.heights()) w.f32(h);

    w.u32(static_cast<std::uint32_t>(world.props.size()));
    for (const Prop& p : world.props) putProp(w, p);

    w.u32(static_cast<std::uint32_t>(world.villages.size()));
    for (const Village& v : world.villages) putVillage(w, v);

    for (int g = 0; g < tune::kMaxGods; ++g) {
      const God& deity = world.gods[g];
      w.b(deity.active);
      w.b(deity.isPlayer);
      w.b(deity.ai);
      w.b(deity.ruined);
      w.b(deity.temple.founded);
      w.v3(deity.temple.pos);
      w.f32(deity.temple.yaw);
      w.f32(deity.mana);
      w.f32(deity.manaMax);
    }
    for (int g = 0; g < tune::kMaxGods; ++g) {
      const GodAI& brain = world.ai[g];
      w.u8(static_cast<std::uint8_t>(brain.profile));
      w.v3(brain.handPos);
      w.v3(brain.handVel);
      w.i32(brain.devotions);
      w.i32(brain.feeds);
      w.i32(brain.placements);
      w.i32(brain.combines);
      w.i32(brain.gifts);
      w.i32(brain.courtCasts);
      w.f32(brain.thinkTimer_);
      w.f32(brain.cooldown_);
      w.u32(brain.rng_);
    }

    w.b(cam != nullptr);
    if (cam) {
      w.f32(cam->focus[0]);
      w.f32(cam->focus[1]);
      w.f32(cam->focus[2]);
      w.f32(cam->yaw);
      w.f32(cam->distance);
      w.f32(cam->pitchOffset);
    }
  }

  static bool load(World& world, const std::uint8_t* data, std::size_t size,
                   savefile::CamState* camOut) {
    serial::Reader r{data, size};
    if (r.u8() != 'G' || r.u8() != 'S' || r.u8() != 'A' || r.u8() != 'V')
      return false;
    if (r.u32() != savefile::kVersion) return false;
    std::uint32_t seed = r.u32();
    std::uint32_t miracles = r.u32();
    std::uint32_t strokes = r.u32();
    float dayT = r.f32();
    int day = r.i32();
    float spd = r.f32();

    std::uint32_t gridVerts = r.u32();
    if (!r.ok || gridVerts != static_cast<std::uint32_t>(Terrain::GRID + 1))
      return false;
    std::vector<float> heights(gridVerts * gridVerts);
    for (float& h : heights) h = r.f32();
    if (!r.ok) return false;

    world.seed_ = seed;
    world.miracleCounter_ = miracles;
    world.editStroke_ = strokes;
    world.dayCycle = DayCycle{};
    world.dayCycle.t = dayT;
    world.dayCycle.day = day;
    world.dayCycle.secondsPerDay = spd;
    world.terrain.setHeights(heights, seed);

    std::uint32_t np = r.u32();
    if (!r.ok || np > 100000u) return false;
    world.props.assign(np, Prop{});
    for (Prop& p : world.props) getProp(r, p);
    if (!r.ok) return false;

    std::uint32_t nv = r.u32();
    if (!r.ok || nv > 256u) return false;
    world.villages.assign(nv, Village{});
    for (Village& v : world.villages)
      if (!getVillage(r, v)) return false;

    for (int g = 0; g < tune::kMaxGods; ++g) {
      God& deity = world.gods[g];
      deity.active = r.b();
      deity.isPlayer = r.b();
      deity.ai = r.b();
      deity.ruined = r.b();
      deity.temple.founded = r.b();
      deity.temple.pos = r.v3();
      deity.temple.yaw = r.f32();
      deity.mana = r.f32();
      deity.manaMax = r.f32();
    }
    for (int g = 0; g < tune::kMaxGods; ++g) {
      GodAI& brain = world.ai[g];
      brain = GodAI{};
      brain.god = g;
      brain.profile = std::min<int>(2, r.u8());
      brain.handPos = r.v3();
      brain.handVel = r.v3();
      brain.devotions = r.i32();
      brain.feeds = r.i32();
      brain.placements = r.i32();
      brain.combines = r.i32();
      brain.gifts = r.i32();
      brain.courtCasts = r.i32();
      brain.thinkTimer_ = r.f32();
      brain.cooldown_ = r.f32();
      brain.rng_ = r.u32();
    }

    bool hasCam = r.b();
    if (hasCam) {
      savefile::CamState cs;
      cs.focus[0] = r.f32();
      cs.focus[1] = r.f32();
      cs.focus[2] = r.f32();
      cs.yaw = r.f32();
      cs.distance = r.f32();
      cs.pitchOffset = r.f32();
      if (camOut) *camOut = cs;
    }
    if (!r.ok) return false;

    world.handPos = glm::vec3(0.0f, 1.0e9f, 0.0f);
    world.handSpeed = 0.0f;
    world.rebuildObstacleGrid();
    return true;
  }
};

namespace savefile {

void save(World& world, std::vector<std::uint8_t>& out, const CamState* cam) {
  SaveIO::save(world, out, cam);
}

bool load(World& world, const std::uint8_t* data, std::size_t size,
          CamState* camOut) {
  return SaveIO::load(world, data, size, camOut);
}

bool saveFile(World& world, const char* path, const CamState* cam) {
  std::vector<std::uint8_t> buf;
  save(world, buf, cam);
  std::FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  std::size_t written = std::fwrite(buf.data(), 1, buf.size(), fp);
  std::fclose(fp);
  return written == buf.size();
}

bool loadFile(World& world, const char* path, CamState* camOut) {
  std::FILE* fp = std::fopen(path, "rb");
  if (!fp) return false;
  std::fseek(fp, 0, SEEK_END);
  long len = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (len <= 0) {
    std::fclose(fp);
    return false;
  }
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
  std::size_t got = std::fread(buf.data(), 1, buf.size(), fp);
  std::fclose(fp);
  if (got != buf.size()) return false;
  return load(world, buf.data(), buf.size(), camOut);
}

}  // namespace savefile
