#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class World;

// .gmap map files (M6). A map is a STARTING CONDITION - the heightfield plus
// entity specs (trees/rocks, villages as site+owner+preset+stores, temples) -
// never a mid-game save (full-state saves are M7's). Loading rebuilds the
// world through the same founding paths the generator uses, so loaded worlds
// obey every invariant for free; saving derives the specs back from a live
// world. save -> load -> save is byte-stable. Sim-side, no GL, deterministic.
namespace mapfile {

inline constexpr std::uint32_t kVersion = 1;

// Serialize the world's starting-relevant state.
void save(const World& world, std::vector<std::uint8_t>& out);

// Rebuild `world` from a buffer. Returns false on bad magic/version/layout
// (the world is then left in an unspecified but safe state - rebuild it).
bool load(World& world, const std::uint8_t* data, std::size_t size);

bool saveFile(const World& world, const char* path);
bool loadFile(World& world, const char* path);

}  // namespace mapfile
