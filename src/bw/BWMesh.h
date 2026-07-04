#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../Mesh.h"
#include "BWPack.h"

// AllMeshes.g3d reader: the pack's MESHES block ("MKJC" + offset table) holds
// every L3D mesh at a fixed index; texture blocks hold DDS (DXT1/DXT3) images
// keyed by the id L3D materials reference. bake() turns one mesh into our
// 9-float MeshData - triangles rewound to CCW, per-vertex color sampled from
// the mesh's own texture (or the material color when untextured), positions
// scaled to meters. Formats: docs/plan-assets.md §7.2. GL-free.
namespace bw {

struct BakedMesh {
  MeshData data;
  glm::vec3 bbMin{0.0f}, bbMax{0.0f};  // after scaling
  int drawnSubmeshes = 0, skippedSubmeshes = 0;
  int texturedPrims = 0, plainPrims = 0;
};

class MeshPack {
 public:
  bool load(const std::string& path);
  bool parse(Pack pack);  // takes the container over; tests feed this

  bool loaded() const { return !meshSlices_.empty(); }
  int meshCount() const { return static_cast<int>(meshSlices_.size()); }
  int textureCount() const { return static_cast<int>(textureIds_.size()); }

  // Decode mesh `index` at `scale` meters per B&W unit. False (out untouched)
  // on any malformed data or an empty result.
  bool bake(int index, float scale, BakedMesh& out) const;

 private:
  struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;  // 4 bytes per texel, row 0 = top
  };
  const Image* texture(std::uint32_t id) const;

  Pack pack_;
  const std::uint8_t* meshesBody_ = nullptr;  // into pack_ bytes
  std::uint32_t meshesSize_ = 0;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> meshSlices_;  // offset,size
  std::vector<std::uint32_t> textureIds_;
  mutable std::map<std::uint32_t, Image> textureCache_;
};

}  // namespace bw
