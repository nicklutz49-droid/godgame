#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class World;

// .sav game saves (M7). Unlike a .gmap (a starting condition), a save is the
// COMPLETE mid-game sim state - every prop, villager, village counter, god,
// and AI brain, private rng streams included - restored field-for-field so a
// loaded game continues bit-for-bit like the original. Anything held by a
// hand is settled in place first, so a save is always a valid world.
// save -> load -> save is byte-stable. Sim-side, no GL, deterministic.
namespace savefile {

inline constexpr std::uint32_t kVersion = 1;

// The view, restored for the player's comfort (ignored headless).
struct CamState {
  float focus[3] = {0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float distance = 160.0f;
  float pitchOffset = 0.0f;
};

// Serialize the complete world (settling held things first - hence non-const).
void save(World& world, std::vector<std::uint8_t>& out,
          const CamState* cam = nullptr);

// Restore a world from a buffer. Returns false on bad magic/version/layout
// (the world is then unspecified but safe - rebuild it). camOut is filled
// when the save carries a view and camOut is non-null.
bool load(World& world, const std::uint8_t* data, std::size_t size,
          CamState* camOut = nullptr);

bool saveFile(World& world, const char* path, const CamState* cam = nullptr);
bool loadFile(World& world, const char* path, CamState* camOut = nullptr);

}  // namespace savefile
