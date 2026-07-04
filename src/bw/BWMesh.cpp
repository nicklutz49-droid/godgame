#include "BWMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bw {

namespace {

std::uint32_t rd32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint16_t rd16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

float rdf(const std::uint8_t* p) {
  std::uint32_t v = rd32(p);
  float f;
  std::memcpy(&f, &v, sizeof f);
  return f;
}

// --- DXT (S3TC) block decompression, from the public format description ----

struct Rgb {
  std::uint8_t r, g, b;
};

Rgb expand565(std::uint16_t c) {
  return {static_cast<std::uint8_t>(((c >> 11) & 31) * 255 / 31),
          static_cast<std::uint8_t>(((c >> 5) & 63) * 255 / 63),
          static_cast<std::uint8_t>((c & 31) * 255 / 31)};
}

// One 4x4 color block (8 bytes) into out[16] RGB; `opaque` forces 4-color
// mode (DXT3's color block never uses the punch-through encoding).
void decodeColorBlock(const std::uint8_t* p, Rgb out[16], bool opaque) {
  std::uint16_t c0 = rd16(p), c1 = rd16(p + 2);
  std::uint32_t bits = rd32(p + 4);
  Rgb pal[4];
  pal[0] = expand565(c0);
  pal[1] = expand565(c1);
  if (opaque || c0 > c1) {
    pal[2] = {static_cast<std::uint8_t>((2 * pal[0].r + pal[1].r) / 3),
              static_cast<std::uint8_t>((2 * pal[0].g + pal[1].g) / 3),
              static_cast<std::uint8_t>((2 * pal[0].b + pal[1].b) / 3)};
    pal[3] = {static_cast<std::uint8_t>((pal[0].r + 2 * pal[1].r) / 3),
              static_cast<std::uint8_t>((pal[0].g + 2 * pal[1].g) / 3),
              static_cast<std::uint8_t>((pal[0].b + 2 * pal[1].b) / 3)};
  } else {
    pal[2] = {static_cast<std::uint8_t>((pal[0].r + pal[1].r) / 2),
              static_cast<std::uint8_t>((pal[0].g + pal[1].g) / 2),
              static_cast<std::uint8_t>((pal[0].b + pal[1].b) / 2)};
    pal[3] = {0, 0, 0};  // punch-through black
  }
  for (int t = 0; t < 16; ++t) out[t] = pal[(bits >> (t * 2)) & 3];
}

// DXT1 (8 B/block) or DXT3 (16 B: 4-bit alpha plane + color block) into RGBA8.
bool decodeDxt(const std::uint8_t* texels, std::size_t texelBytes, int w, int h,
               bool dxt3, std::vector<std::uint8_t>& rgba) {
  int bw4 = (w + 3) / 4, bh4 = (h + 3) / 4;
  std::size_t blockSize = dxt3 ? 16 : 8;
  if (texelBytes < static_cast<std::size_t>(bw4) * bh4 * blockSize) return false;
  rgba.assign(static_cast<std::size_t>(w) * h * 4, 255);
  for (int by = 0; by < bh4; ++by) {
    for (int bx = 0; bx < bw4; ++bx) {
      const std::uint8_t* p = texels + (static_cast<std::size_t>(by) * bw4 + bx) * blockSize;
      Rgb c[16];
      decodeColorBlock(dxt3 ? p + 8 : p, c, dxt3);
      for (int t = 0; t < 16; ++t) {
        int x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
        if (x >= w || y >= h) continue;
        std::uint8_t* dst = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
        dst[0] = c[t].r;
        dst[1] = c[t].g;
        dst[2] = c[t].b;
        if (dxt3) {
          std::uint8_t a4 = (p[t / 2] >> ((t & 1) * 4)) & 0xF;
          dst[3] = static_cast<std::uint8_t>(a4 * 17);
        }
      }
    }
  }
  return true;
}

constexpr std::uint32_t kAbsent = 0xFFFFFFFFu;

}  // namespace

bool MeshPack::load(const std::string& path) {
  Pack p;
  if (!p.load(path)) return false;
  return parse(std::move(p));
}

bool MeshPack::parse(Pack pack) {
  meshesBody_ = nullptr;
  meshesSize_ = 0;
  meshSlices_.clear();
  textureIds_.clear();
  textureCache_.clear();

  std::uint32_t infoSize = 0;
  const std::uint8_t* info = pack.block("INFO", &infoSize);
  if (info && infoSize >= 4) {
    std::uint32_t n = rd32(info);
    if (n <= (infoSize - 4) / 8) {
      for (std::uint32_t t = 0; t < n; ++t) textureIds_.push_back(rd32(info + 4 + t * 8));
    }
  }

  std::uint32_t size = 0;
  const std::uint8_t* body = pack.block("MESHES", &size);
  if (!body || size < 8) return false;
  if (std::memcmp(body, "MKJC", 4) != 0) return false;
  std::uint32_t count = rd32(body + 4);
  if (count == 0 || count > 100000 || 8 + static_cast<std::size_t>(count) * 4 > size)
    return false;

  // The offset table is relative to the block body; meshes are sized by the
  // gap to the next offset (last one runs to the end of the block).
  std::vector<std::pair<std::uint32_t, std::uint32_t>> slices;
  slices.reserve(count);
  for (std::uint32_t m = 0; m < count; ++m) {
    std::uint32_t off = rd32(body + 8 + m * 4);
    std::uint32_t next = m + 1 < count ? rd32(body + 8 + (m + 1) * 4) : size;
    if (off > size || next > size || next < off) return false;
    slices.push_back({off, next - off});
  }

  pack_ = std::move(pack);
  meshesBody_ = body;  // pack_ owns the bytes; pointer stays valid
  meshesSize_ = size;
  meshSlices_ = std::move(slices);
  // Re-resolve against the moved-to pack (the vector may have reallocated
  // nowhere - block() returns into pack_ storage - but be explicit).
  meshesBody_ = pack_.block("MESHES", &meshesSize_);
  return meshesBody_ != nullptr;
}

// Texture blocks are named with the lowercase hex of their id and carry
// {u32 size, u32 id, u32 type, u32 ddsSize} + a DDS file minus its 4-byte
// magic (124-byte header, then the top mip's texels).
const MeshPack::Image* MeshPack::texture(std::uint32_t id) const {
  auto it = textureCache_.find(id);
  if (it != textureCache_.end()) return it->second.w > 0 ? &it->second : nullptr;
  Image& img = textureCache_[id];  // negative cache: w stays 0 on failure

  char name[16];
  std::snprintf(name, sizeof name, "%x", id);
  std::uint32_t size = 0;
  const std::uint8_t* b = pack_.block(name, &size);
  if (!b || size < 16 + 124) return nullptr;
  if (rd32(b + 4) != id) return nullptr;
  const std::uint8_t* dds = b + 16;
  std::uint32_t ddsAvail = size - 16;
  if (rd32(dds) != 124) return nullptr;  // header size field
  int h = static_cast<int>(rd32(dds + 8));
  int w = static_cast<int>(rd32(dds + 12));
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return nullptr;
  // DDS_HEADER: 7 u32 fields + 44 reserved bytes put DDS_PIXELFORMAT at 72;
  // its fourCC sits 8 bytes in.
  const std::uint8_t* fourCC = dds + 72 + 8;
  bool dxt1 = std::memcmp(fourCC, "DXT1", 4) == 0;
  bool dxt3 = std::memcmp(fourCC, "DXT3", 4) == 0;
  if (!dxt1 && !dxt3) return nullptr;
  if (!decodeDxt(dds + 124, ddsAvail - 124, w, h, dxt3, img.rgba)) return nullptr;
  img.w = w;
  img.h = h;
  return &img;
}

bool MeshPack::bake(int index, float scale, BakedMesh& out) const {
  if (index < 0 || index >= meshCount() || !meshesBody_) return false;
  auto [blobOff, blobSize] = meshSlices_[index];
  const std::uint8_t* blob = meshesBody_ + blobOff;
  // Every read below is bounds-checked against the blob.
  auto in = [&](std::uint32_t off, std::uint32_t need) {
    return off <= blobSize && need <= blobSize - off;
  };

  if (!in(0, 76) || std::memcmp(blob, "L3D0", 4) != 0) return false;
  std::uint32_t submeshCount = rd32(blob + 12);
  std::uint32_t submeshTable = rd32(blob + 16);
  if (submeshCount == 0 || submeshCount > 4096) return false;
  if (submeshTable == kAbsent || !in(submeshTable, submeshCount * 4)) return false;

  BakedMesh baked;
  glm::vec3 bbMin(1e9f), bbMax(-1e9f);
  for (std::uint32_t s = 0; s < submeshCount; ++s) {
    std::uint32_t sub = rd32(blob + submeshTable + s * 4);
    if (!in(sub, 20)) return false;
    std::uint32_t flags = rd32(blob + sub);
    std::uint32_t status = (flags >> 4) & 0x3F;
    bool isPhysics = (flags >> 13) & 1;
    std::uint32_t lod = (flags >> 29) & 7;
    // Draw the base LOD of plain visible submeshes; construction-stage
    // variants (status != 0), physics proxies, and lower LODs stay out.
    if (status != 0 || isPhysics || (lod != 0 && !(lod & 1))) {
      ++baked.skippedSubmeshes;
      continue;
    }
    std::uint32_t numPrims = rd32(blob + sub + 4);
    std::uint32_t primTable = rd32(blob + sub + 8);
    if (numPrims > 4096 || primTable == kAbsent || !in(primTable, numPrims * 4))
      return false;
    ++baked.drawnSubmeshes;

    for (std::uint32_t pi = 0; pi < numPrims; ++pi) {
      std::uint32_t prim = rd32(blob + primTable + pi * 4);
      if (!in(prim, 48)) return false;
      std::uint32_t skinId = rd32(blob + prim + 8);
      std::uint32_t colorBgra = rd32(blob + prim + 12);
      std::uint32_t numVerts = rd32(blob + prim + 16);
      std::uint32_t vertsOff = rd32(blob + prim + 20);
      std::uint32_t numTris = rd32(blob + prim + 24);
      std::uint32_t trisOff = rd32(blob + prim + 28);
      if (numVerts == 0 || numTris == 0) continue;
      if (numVerts > 1000000 || numTris > 1000000) return false;
      if (!in(vertsOff, numVerts * 32) || !in(trisOff, numTris * 6)) return false;

      const Image* img = skinId != kAbsent ? texture(skinId) : nullptr;
      if (img)
        ++baked.texturedPrims;
      else
        ++baked.plainPrims;
      glm::vec3 matColor(((colorBgra >> 16) & 0xFF) / 255.0f,
                         ((colorBgra >> 8) & 0xFF) / 255.0f,
                         (colorBgra & 0xFF) / 255.0f);
      if (!img && colorBgra == 0) matColor = glm::vec3(0.72f);  // uncolored: clay

      std::uint32_t base = baked.data.vertexCount();
      for (std::uint32_t v = 0; v < numVerts; ++v) {
        const std::uint8_t* vp = blob + vertsOff + v * 32;
        glm::vec3 pos(rdf(vp), rdf(vp + 4), rdf(vp + 8));
        float u = rdf(vp + 12), tv = rdf(vp + 16);
        glm::vec3 nrm(rdf(vp + 20), rdf(vp + 24), rdf(vp + 28));
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
          return false;
        glm::vec3 color = matColor;
        if (img) {
          // Wrap-sample the decoded image; rows are top-down exactly like
          // the D3D-convention UVs, so no flip is wanted for a CPU bake.
          float fu = u - std::floor(u), fv = tv - std::floor(tv);
          int tx = std::min(static_cast<int>(fu * img->w), img->w - 1);
          int ty = std::min(static_cast<int>(fv * img->h), img->h - 1);
          const std::uint8_t* px =
              img->rgba.data() + (static_cast<std::size_t>(ty) * img->w + tx) * 4;
          color = glm::vec3(px[0], px[1], px[2]) / 255.0f;
        }
        float len = glm::length(nrm);
        nrm = len > 1e-5f ? nrm / len : glm::vec3(0.0f, 1.0f, 0.0f);
        pos *= scale;
        baked.data.addVertex(pos, nrm, color);
        bbMin = glm::min(bbMin, pos);
        bbMax = glm::max(bbMax, pos);
      }
      for (std::uint32_t t = 0; t < numTris; ++t) {
        const std::uint8_t* tp = blob + trisOff + t * 6;
        std::uint32_t a = rd16(tp), b = rd16(tp + 2), c = rd16(tp + 4);
        if (a >= numVerts || b >= numVerts || c >= numVerts) return false;
        // L3D winds clockwise-front; our pipeline culls to CCW-front.
        baked.data.addTriangle(base + a, base + c, base + b);
      }
    }
  }
  if (baked.data.indices.empty()) return false;
  baked.bbMin = bbMin;
  baked.bbMax = bbMax;
  out = std::move(baked);
  return true;
}

}  // namespace bw
