#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "BWLand.h"
#include "BWMesh.h"
#include "BWMeshMap.h"

// The facade the game talks to. init() points it at the owner's Black &
// White installation (--bw <dir>); everything else degrades to "not there"
// so every call site keeps its procedural fallback. Nothing is extracted,
// converted to disk, or written anywhere - assets live only in this
// process's memory (docs/plan-assets.md §0). GL-free.
namespace bw {

class Assets {
 public:
  // Scans the directory (case-insensitively - GOG/Wine installs vary).
  // False and inert when it does not look like a B&W install.
  bool init(const std::string& installDir);
  bool available() const { return available_; }
  const std::string& dir() const { return dir_; }

  // Data/Landscape/Land<n>.lnd resolved on disk, or "" if absent.
  std::string landPath(int n) const;
  bool hasLand(int n) const { return !landPath(n).empty(); }

  // Load + resample Land<n> onto a (gridN+1)^2 vertex grid (Terrain layout).
  bool landHeights(int n, std::vector<float>& out, int gridN, float outSize,
                   float seabed) const;

  // The baked mesh for a placeholder slot; nullptr when missing/unparseable.
  // Cached after the first bake.
  const BakedMesh* mesh(Slot slot);

  // --bw-report: parse everything, print an inventory (lands, textures,
  // every slot's mesh stats). Returns a process exit code.
  int report();

 private:
  std::string dir_;
  bool available_ = false;
  bool meshesLoaded_ = false;
  MeshPack meshes_;
  std::array<std::optional<BakedMesh>, static_cast<int>(Slot::Count)> baked_;

  bool ensureMeshes();
  bool hasLandUnchecked(int n) const;  // pre-available_ probe used by init()
};

// Resolve a '/'-separated relative path under `base`, matching each
// component case-insensitively. "" when any component is missing.
std::string resolveCaseInsensitive(const std::string& base, const std::string& relative);

}  // namespace bw
