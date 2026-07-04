#pragma once

#include <cstdint>
#include <string>
#include <vector>

// .lnd landscape reader: raw little-endian struct dump (no magic, no
// compression) laid out as header / low-res textures / blocks / countries /
// materials / noise / bump (docs/plan-assets.md §7.1). We keep only what the
// game needs — the 512x512 cell grid (32x32 blocks of 16x16 cells; unstored
// blocks are open sea) with altitude, water flags, and the split bit — and
// resample it onto our Terrain grid. Heights are DATA: determinism rules are
// untouched. GL-free; a failed parse leaves the land empty.
namespace bw {

struct LandCell {
  std::uint8_t altitude = 0;
  // bits 0-3 country, 0x10 hasWater, 0x20 coastLine, 0x40 fullWater, 0x80 split
  std::uint8_t properties = 0;
  std::uint8_t r = 0, g = 0, b = 0;

  bool water() const { return (properties & 0x50) != 0; }
  bool fullWater() const { return (properties & 0x40) != 0; }
  bool coast() const { return (properties & 0x20) != 0; }
  bool split() const { return (properties & 0x80) != 0; }
};

class Land {
 public:
  static constexpr int kCells = 512;           // 32 blocks x 16 cells per side
  static constexpr float kHeightUnit = 0.67f;  // B&W world Y per altitude unit

  bool load(const std::string& path);
  bool parse(const std::uint8_t* data, std::size_t size);

  bool loaded() const { return !cells_.empty(); }
  int storedBlocks() const { return storedBlocks_; }
  // Mean coastline altitude in B&W Y units - the land's own waterline.
  float coastAltitude() const { return coastAlt_; }

  const LandCell& cell(int x, int z) const;      // clamped; ocean outside
  float heightAt(float cx, float cz) const;      // split-aware, B&W Y units

  // Fill a (gridN+1)^2 vertex grid spanning outSize world units (row order
  // out[j*(gridN+1)+i], i along +x), XZ at 1 cell = 1 meter (B&W's 5120-unit
  // island x0.1 = our 512). Heights: (bwY - coastline) * scale, water cells
  // clamped below our y=0, everything kept inside [seabed, seabed+range].
  void resampleHeights(std::vector<float>& out, int gridN, float outSize,
                       float scale, float seabed) const;

 private:
  std::vector<LandCell> cells_;  // kCells*kCells, [z*kCells + x]
  int storedBlocks_ = 0;
  float coastAlt_ = 0.0f;
};

}  // namespace bw
