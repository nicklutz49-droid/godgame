#include "MapFile.h"

#include <cstdio>
#include <cstring>

#include "Tuning.h"
#include "World.h"

namespace mapfile {

namespace {

// Little-endian byte-level put/get: identical files on every platform.
void putU8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }

void putU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  b.push_back(static_cast<std::uint8_t>(v & 0xFF));
  b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void putI32(std::vector<std::uint8_t>& b, std::int32_t v) {
  putU32(b, static_cast<std::uint32_t>(v));
}

void putF32(std::vector<std::uint8_t>& b, float f) {
  std::uint32_t v;
  std::memcpy(&v, &f, sizeof v);
  putU32(b, v);
}

struct Reader {
  const std::uint8_t* p;
  std::size_t n;
  std::size_t off = 0;
  bool ok = true;

  std::uint8_t u8() {
    if (off + 1 > n) {
      ok = false;
      return 0;
    }
    return p[off++];
  }
  std::uint32_t u32() {
    if (off + 4 > n) {
      ok = false;
      return 0;
    }
    std::uint32_t v = static_cast<std::uint32_t>(p[off]) |
                      static_cast<std::uint32_t>(p[off + 1]) << 8 |
                      static_cast<std::uint32_t>(p[off + 2]) << 16 |
                      static_cast<std::uint32_t>(p[off + 3]) << 24;
    off += 4;
    return v;
  }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  float f32() {
    std::uint32_t v = u32();
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
  }
};

}  // namespace

void save(const World& world, std::vector<std::uint8_t>& out) {
  out.clear();
  out.push_back('G');
  out.push_back('M');
  out.push_back('A');
  out.push_back('P');
  putU32(out, kVersion);
  putU32(out, world.seed());
  putU32(out, static_cast<std::uint32_t>(Terrain::GRID + 1));
  for (float h : world.terrain.heights()) putF32(out, h);

  // Vegetation and boulders. Stumps, felled trees, and loose resources are
  // play state, not map state - a map always describes a fresh morning.
  std::uint32_t propCount = 0;
  for (const Prop& p : world.props)
    if (p.alive && !p.held && p.carrier < 0 && !p.felled &&
        (p.type == PropType::Tree || p.type == PropType::Rock))
      ++propCount;
  putU32(out, propCount);
  for (const Prop& p : world.props) {
    if (!(p.alive && !p.held && p.carrier < 0 && !p.felled &&
          (p.type == PropType::Tree || p.type == PropType::Rock)))
      continue;
    putU8(out, p.type == PropType::Tree ? 0 : 1);
    putU8(out, static_cast<std::uint8_t>(p.variant));
    putF32(out, p.pos.x);
    putF32(out, p.pos.z);
    putF32(out, p.scale);
    putF32(out, p.baseYaw);
  }

  std::uint32_t villageCount = 0;
  for (const Village& v : world.villages)
    if (v.founded) ++villageCount;
  putU32(out, villageCount);
  for (const Village& v : world.villages) {
    if (!v.founded) continue;
    putF32(out, v.center.x);
    putF32(out, v.center.z);
    putU8(out, static_cast<std::uint8_t>(static_cast<std::int8_t>(v.owner)));
    putU8(out, static_cast<std::uint8_t>(v.startPreset));
    putI32(out, v.food);
    putI32(out, v.wood);
  }

  std::uint32_t templeCount = 0;
  for (int g = 0; g < tune::kMaxGods; ++g)
    if (world.gods[g].active && world.gods[g].temple.founded) ++templeCount;
  putU32(out, templeCount);
  for (int g = 0; g < tune::kMaxGods; ++g) {
    const God& deity = world.gods[g];
    if (!deity.active || !deity.temple.founded) continue;
    putU8(out, static_cast<std::uint8_t>(g));
    putF32(out, deity.temple.pos.x);
    putF32(out, deity.temple.pos.z);
    putF32(out, deity.temple.yaw);
  }
}

bool load(World& world, const std::uint8_t* data, std::size_t size) {
  Reader r{data, size};
  if (r.u8() != 'G' || r.u8() != 'M' || r.u8() != 'A' || r.u8() != 'P')
    return false;
  if (r.u32() != kVersion) return false;
  std::uint32_t seed = r.u32();
  std::uint32_t gridVerts = r.u32();
  if (!r.ok || gridVerts != static_cast<std::uint32_t>(Terrain::GRID + 1))
    return false;

  // Parse everything before touching the world.
  std::vector<float> heights(gridVerts * gridVerts);
  for (float& h : heights) h = r.f32();

  struct PropSpec {
    std::uint8_t type, variant;
    float x, z, scale, yaw;
  };
  std::uint32_t propCount = r.u32();
  if (!r.ok || propCount > 100000u) return false;
  std::vector<PropSpec> props(propCount);
  for (PropSpec& s : props) {
    s.type = r.u8();
    s.variant = r.u8();
    s.x = r.f32();
    s.z = r.f32();
    s.scale = r.f32();
    s.yaw = r.f32();
  }

  struct VillageSpec {
    float x, z;
    std::int8_t owner;
    std::uint8_t preset;
    std::int32_t food, wood;
  };
  std::uint32_t villageCount = r.u32();
  if (!r.ok || villageCount > 256u) return false;
  std::vector<VillageSpec> villages(villageCount);
  for (VillageSpec& s : villages) {
    s.x = r.f32();
    s.z = r.f32();
    s.owner = static_cast<std::int8_t>(r.u8());
    s.preset = r.u8();
    s.food = r.i32();
    s.wood = r.i32();
  }

  struct TempleSpec {
    std::uint8_t god;
    float x, z, yaw;
  };
  std::uint32_t templeCount = r.u32();
  if (!r.ok || templeCount > static_cast<std::uint32_t>(tune::kMaxGods))
    return false;
  std::vector<TempleSpec> temples(templeCount);
  for (TempleSpec& s : temples) {
    s.god = r.u8();
    s.x = r.f32();
    s.z = r.f32();
    s.yaw = r.f32();
  }
  if (!r.ok) return false;

  // Rebuild: blank world, the saved ground, then every entity through the
  // standard founding paths (terraform=false - the ground already carries
  // the terraces, so layout reads identical heights and stays bit-exact).
  world.buildBlank(seed);
  world.terrain.setHeights(heights, seed);
  for (const VillageSpec& s : villages) {
    int idx = world.foundVillageFromSpec(glm::vec2(s.x, s.z), s.owner, s.preset,
                                         false);
    world.villages[idx].food = s.food;
    world.villages[idx].wood = s.wood;
  }
  for (const TempleSpec& s : temples) {
    if (s.god >= tune::kMaxGods) continue;
    world.wakeGod(s.god);
    Temple& t = world.gods[s.god].temple;
    t.founded = true;
    t.pos = glm::vec3(s.x, world.terrain.heightAt(s.x, s.z), s.z);
    t.yaw = s.yaw;
  }
  for (const PropSpec& s : props) {
    Prop p;
    p.type = s.type == 0 ? PropType::Tree : PropType::Rock;
    p.variant = s.variant % 3;
    p.scale = s.scale;
    p.radius = (p.type == PropType::Tree ? 1.6f : 0.9f) * p.scale;
    p.baseYaw = s.yaw;
    if (p.type == PropType::Tree)
      p.resource = static_cast<float>(tune::kChopSwings);
    p.pos = glm::vec3(s.x, 0.0f, s.z);
    p.pos.y = world.restHeight(p);
    p.rot = glm::angleAxis(p.baseYaw, glm::vec3(0, 1, 0));
    world.spawnProp(p);
  }
  // Hands rest at their (just-seated) temples.
  for (int g = 0; g < tune::kMaxGods; ++g) world.ai[g].reset(world, g);
  return true;
}

bool saveFile(const World& world, const char* path) {
  std::vector<std::uint8_t> buf;
  save(world, buf);
  std::FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  std::size_t written = std::fwrite(buf.data(), 1, buf.size(), fp);
  std::fclose(fp);
  return written == buf.size();
}

bool loadFile(World& world, const char* path) {
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
  return load(world, buf.data(), buf.size());
}

}  // namespace mapfile
