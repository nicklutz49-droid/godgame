#include "BWAssets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "../Tuning.h"

namespace bw {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::string resolveCaseInsensitive(const std::string& base, const std::string& relative) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path cur(base);
  if (!fs::exists(cur, ec)) return "";

  std::size_t start = 0;
  while (start < relative.size()) {
    std::size_t slash = relative.find('/', start);
    std::string part = relative.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    start = slash == std::string::npos ? relative.size() : slash + 1;
    if (part.empty()) continue;

    fs::path exact = cur / part;
    if (fs::exists(exact, ec)) {
      cur = exact;
      continue;
    }
    std::string want = lower(part);
    bool found = false;
    for (const fs::directory_entry& e : fs::directory_iterator(cur, ec)) {
      if (lower(e.path().filename().string()) == want) {
        cur = e.path();
        found = true;
        break;
      }
    }
    if (!found) return "";
  }
  return cur.string();
}

bool Assets::init(const std::string& installDir) {
  dir_ = installDir;
  available_ = false;
  meshesLoaded_ = false;
  meshes_ = MeshPack{};
  baked_ = {};
  if (installDir.empty()) return false;

  // It counts as an install if either the mesh pack or any landscape exists.
  bool any = !resolveCaseInsensitive(dir_, "Data/AllMeshes.g3d").empty();
  for (int n = 1; !any && n <= 5; ++n) any = hasLandUnchecked(n);
  available_ = any;
  if (!available_)
    std::printf("bw: %s does not look like a Black & White install "
                "(no Data/AllMeshes.g3d, no Data/Landscape/Land<n>.lnd)\n",
                dir_.c_str());
  return available_;
}

bool Assets::hasLandUnchecked(int n) const {
  return !resolveCaseInsensitive(
              dir_, "Data/Landscape/Land" + std::to_string(n) + ".lnd")
              .empty();
}

std::string Assets::landPath(int n) const {
  if (!available_ || n < 1 || n > 5) return "";
  return resolveCaseInsensitive(dir_,
                                "Data/Landscape/Land" + std::to_string(n) + ".lnd");
}

bool Assets::landHeights(int n, std::vector<float>& out, int gridN, float outSize,
                         float seabed) const {
  std::string path = landPath(n);
  if (path.empty()) return false;
  Land land;
  if (!land.load(path)) {
    std::printf("bw: could not parse %s\n", path.c_str());
    return false;
  }
  land.resampleHeights(out, gridN, outSize, tune::kBwLandHeightScale, seabed);
  return true;
}

bool Assets::ensureMeshes() {
  if (meshesLoaded_) return meshes_.loaded();
  meshesLoaded_ = true;
  std::string path = resolveCaseInsensitive(dir_, "Data/AllMeshes.g3d");
  if (path.empty()) return false;
  if (!meshes_.load(path)) {
    std::printf("bw: could not parse %s\n", path.c_str());
    return false;
  }
  return true;
}

const BakedMesh* Assets::mesh(Slot slot) {
  if (!available_) return nullptr;
  int idx = static_cast<int>(slot);
  if (idx < 0 || idx >= static_cast<int>(Slot::Count)) return nullptr;
  if (baked_[idx].has_value())
    return baked_[idx]->data.indices.empty() ? nullptr : &*baked_[idx];
  baked_[idx].emplace();  // negative cache: stays empty on failure
  if (!ensureMeshes()) return nullptr;

  for (const SlotBinding& b : kSlotBindings) {
    if (b.slot != slot) continue;
    BakedMesh m;
    if (b.meshIndex < meshes_.meshCount() &&
        meshes_.bake(b.meshIndex, tune::kBwMeshScale * b.scaleMul, m)) {
      *baked_[idx] = std::move(m);
      return &*baked_[idx];
    }
    std::printf("bw: mesh %d (%s) did not bake - placeholder stays\n",
                b.meshIndex, b.bwName);
    return nullptr;
  }
  return nullptr;  // slot has no binding
}

int Assets::report() {
  std::printf("bw install:  %s\n", dir_.c_str());
  if (!available_) return 1;

  for (int n = 1; n <= 5; ++n) {
    std::string path = landPath(n);
    if (path.empty()) {
      std::printf("Land%d:       missing\n", n);
      continue;
    }
    Land land;
    if (!land.load(path)) {
      std::printf("Land%d:       PARSE FAILED (%s)\n", n, path.c_str());
      continue;
    }
    int waterCells = 0, landCells = 0;
    std::uint8_t lo = 255, hi = 0;
    for (int z = 0; z < Land::kCells; ++z)
      for (int x = 0; x < Land::kCells; ++x) {
        const LandCell& c = land.cell(x, z);
        if (c.water())
          ++waterCells;
        else if (c.altitude > 0)
          ++landCells;
        lo = std::min(lo, c.altitude);
        hi = std::max(hi, c.altitude);
      }
    std::printf("Land%d:       %d blocks | altitude %d..%d | waterline %.1f | "
                "%.0f%% water cells | %.0f%% land cells\n",
                n, land.storedBlocks(), lo, hi, land.coastAltitude(),
                100.0f * waterCells / (Land::kCells * Land::kCells),
                100.0f * landCells / (Land::kCells * Land::kCells));
  }

  if (!ensureMeshes()) {
    std::printf("AllMeshes:   missing or unparseable\n");
    return 1;
  }
  std::printf("AllMeshes:   %d meshes | %d textures declared\n",
              meshes_.meshCount(), meshes_.textureCount());
  std::printf("%-12s %-26s %5s %6s %6s  %-22s %s\n", "slot", "bw mesh", "sub",
              "verts", "tris", "bbox (m)", "prims tex/plain");
  for (const SlotBinding& b : kSlotBindings) {
    BakedMesh m;
    bool ok = b.meshIndex < meshes_.meshCount() &&
              meshes_.bake(b.meshIndex, tune::kBwMeshScale * b.scaleMul, m);
    if (!ok) {
      std::printf("%-12d %-26s BAKE FAILED\n", static_cast<int>(b.slot), b.bwName);
      continue;
    }
    glm::vec3 ext = m.bbMax - m.bbMin;
    char bbox[32];
    std::snprintf(bbox, sizeof bbox, "%.1fx%.1fx%.1f", ext.x, ext.y, ext.z);
    std::printf("%-12d %-26s %2d+%-2d %6u %6zu  %-22s %d/%d\n",
                static_cast<int>(b.slot), b.bwName, m.drawnSubmeshes,
                m.skippedSubmeshes, m.data.vertexCount(),
                m.data.indices.size() / 3, bbox, m.texturedPrims, m.plainPrims);
  }
  std::printf("scale: 1 B&W unit = %.2f m (kBwMeshScale) | land height x%.2f "
              "(kBwLandHeightScale)\n",
              tune::kBwMeshScale, tune::kBwLandHeightScale);
  return 0;
}

}  // namespace bw
