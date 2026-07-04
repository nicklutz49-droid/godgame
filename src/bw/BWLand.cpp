#include "BWLand.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bw {

namespace {

constexpr std::size_t kHeaderSize = 1052;
constexpr std::size_t kBlockSize = 2520;
constexpr std::size_t kCountrySize = 3076;
constexpr std::size_t kMaterialSize = 0x20002;
constexpr std::size_t kNoiseSize = 65536;
constexpr std::size_t kCellBytes = 8;
constexpr int kBlockCells = 16;  // quads per block side (17x17 stored vertices)

std::uint32_t rd32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

}  // namespace

bool Land::load(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  if (got != bytes.size()) return false;
  return parse(bytes.data(), bytes.size());
}

bool Land::parse(const std::uint8_t* data, std::size_t size) {
  cells_.clear();
  storedBlocks_ = 0;
  coastAlt_ = 0.0f;
  if (!data || size < kHeaderSize) return false;

  std::uint32_t blockCount = rd32(data);
  const std::uint8_t* lut = data + 4;  // u8[1024], lut[blockX*32 + blockZ]
  std::uint32_t materialCount = rd32(data + 1028);
  std::uint32_t countryCount = rd32(data + 1032);
  std::uint32_t blockSize = rd32(data + 1036);
  std::uint32_t materialSize = rd32(data + 1040);
  std::uint32_t countrySize = rd32(data + 1044);
  std::uint32_t lowResCount = rd32(data + 1048);

  // Sanity: the three size fields are format constants; counts must be sane.
  if (blockSize != kBlockSize || countrySize != kCountrySize ||
      materialSize != kMaterialSize)
    return false;
  if (blockCount < 1 || blockCount > 1025 || lowResCount > 64) return false;

  // Skip the low-res texture records: 16 bytes of ids, then a u32 size that
  // INCLUDES itself, then (size - 4) texel bytes.
  std::size_t off = kHeaderSize;
  for (std::uint32_t t = 0; t < lowResCount; ++t) {
    if (off + 20 > size) return false;
    std::uint32_t recSize = rd32(data + off + 16);
    if (recSize < 4 || recSize - 4 > size - (off + 20)) return false;
    off += 20 + (recSize - 4);
  }

  // Stored blocks: blockCount - 1 of them (block 0 is the unstored open sea).
  std::size_t stored = blockCount - 1;
  if (stored * kBlockSize > size - off) return false;
  std::size_t blocksOff = off;
  off += stored * kBlockSize;

  // Countries + materials + noise + bump must at least fit; content unused
  // in phase A (we keep our painterly vertex colors).
  std::size_t tail = static_cast<std::size_t>(countryCount) * kCountrySize +
                     static_cast<std::size_t>(materialCount) * kMaterialSize +
                     2 * kNoiseSize;
  if (tail > size - off) return false;

  // Place every stored block through the lookup table - per-block index
  // fields on disk are untrustworthy, the table is the authority.
  std::vector<LandCell> grid(static_cast<std::size_t>(kCells) * kCells);
  double coastSum = 0.0, waterSum = 0.0;
  long coastN = 0, waterN = 0;
  for (int bx = 0; bx < 32; ++bx) {
    for (int bz = 0; bz < 32; ++bz) {
      std::uint32_t idx = lut[bx * 32 + bz];  // 1-based; 0 = open sea
      if (idx == 0) continue;
      if (idx > stored) return false;
      const std::uint8_t* block = data + blocksOff + (idx - 1) * kBlockSize;
      ++storedBlocks_;
      // 17x17 cells at 8 bytes, cells[cellX*17 + cellZ]; the 17th row/column
      // duplicates the neighbour block, so only 16x16 enter the grid.
      for (int cx = 0; cx < kBlockCells; ++cx) {
        for (int cz = 0; cz < kBlockCells; ++cz) {
          const std::uint8_t* c = block + (cx * 17 + cz) * kCellBytes;
          LandCell& out = grid[static_cast<std::size_t>(bz * kBlockCells + cz) * kCells +
                               (bx * kBlockCells + cx)];
          out.r = c[0];
          out.g = c[1];
          out.b = c[2];
          out.altitude = c[4];
          out.properties = c[6];
          if (out.coast()) {
            coastSum += out.altitude;
            ++coastN;
          }
          if (out.water()) {
            waterSum += out.altitude;
            ++waterN;
          }
        }
      }
    }
  }
  if (storedBlocks_ == 0) return false;

  // The land's own waterline: coastline cells if any, else water cells.
  if (coastN > 0)
    coastAlt_ = static_cast<float>(coastSum / coastN) * kHeightUnit;
  else if (waterN > 0)
    coastAlt_ = static_cast<float>(waterSum / waterN) * kHeightUnit;

  cells_ = std::move(grid);
  return true;
}

const LandCell& Land::cell(int x, int z) const {
  static const LandCell kOcean{};
  if (cells_.empty() || x < 0 || z < 0 || x >= kCells || z >= kCells) return kOcean;
  return cells_[static_cast<std::size_t>(z) * kCells + x];
}

// Exact ground: each cell quad is two triangles picked by the split bit
// (0: the (x,z)-(x+1,z+1) diagonal, 1: the (x+1,z)-(x,z+1) diagonal).
float Land::heightAt(float cx, float cz) const {
  if (cells_.empty()) return 0.0f;
  cx = std::clamp(cx, 0.0f, static_cast<float>(kCells - 1) - 1e-4f);
  cz = std::clamp(cz, 0.0f, static_cast<float>(kCells - 1) - 1e-4f);
  int x0 = static_cast<int>(cx);
  int z0 = static_cast<int>(cz);
  float fx = cx - x0;
  float fz = cz - z0;
  float a = cell(x0, z0).altitude * kHeightUnit;          // (0,0)
  float b = cell(x0 + 1, z0).altitude * kHeightUnit;      // (1,0)
  float c = cell(x0, z0 + 1).altitude * kHeightUnit;      // (0,1)
  float d = cell(x0 + 1, z0 + 1).altitude * kHeightUnit;  // (1,1)
  if (!cell(x0, z0).split()) {
    // Diagonal a-d.
    return fx >= fz ? a + (b - a) * fx + (d - b) * fz
                    : a + (c - a) * fz + (d - c) * fx;
  }
  // Diagonal b-c.
  return fx + fz <= 1.0f ? a + (b - a) * fx + (c - a) * fz
                         : d + (c - d) * (1.0f - fx) + (b - d) * (1.0f - fz);
}

void Land::resampleHeights(std::vector<float>& out, int gridN, float outSize,
                           float scale, float seabed) const {
  out.assign(static_cast<std::size_t>(gridN + 1) * (gridN + 1), seabed);
  if (cells_.empty() || gridN <= 0) return;
  float cellsPerVertex = static_cast<float>(kCells) / gridN;
  (void)outSize;  // XZ mapping is fixed at 1 cell = 1 meter; outSize documents it
  for (int j = 0; j <= gridN; ++j) {
    for (int i = 0; i <= gridN; ++i) {
      float cx = i * cellsPerVertex;
      float cz = j * cellsPerVertex;
      float h = (heightAt(cx, cz) - coastAlt_) * scale;
      const LandCell& c =
          cell(std::min(static_cast<int>(cx), kCells - 1),
               std::min(static_cast<int>(cz), kCells - 1));
      // The sea is faked by flags in the source data - enforce it in meters.
      if (c.fullWater())
        h = std::min(h, -2.5f);
      else if (c.water())
        h = std::min(h, -0.4f);
      out[static_cast<std::size_t>(j) * (gridN + 1) + i] =
          std::clamp(h, seabed, seabed + 96.0f);
    }
  }
}

}  // namespace bw
