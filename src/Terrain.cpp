#include "Terrain.h"

#include <algorithm>
#include <cmath>

#include "Noise.h"

namespace {

float smoothstep(float e0, float e1, float x) {
  float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace

void Terrain::generate(std::uint32_t seed) {
  seed_ = seed;
  heights_.assign((GRID + 1) * (GRID + 1), SEABED);
  minH_ = 1e9f;
  maxH_ = -1e9f;

  for (int j = 0; j <= GRID; ++j) {
    for (int i = 0; i <= GRID; ++i) {
      float x = -SIZE * 0.5f + SIZE * static_cast<float>(i) / GRID;
      float z = -SIZE * 0.5f + SIZE * static_cast<float>(j) / GRID;

      float base = noise::fbm(x * 0.0055f, z * 0.0055f, 5, seed);

      // Irregular coastline: warp the radial falloff with low-frequency noise.
      float warp = noise::fbm(x * 0.004f + 7.3f, z * 0.004f + 2.1f, 3, seed + 77u);
      float d = std::sqrt(x * x + z * z) / (SIZE * 0.42f);
      d += (warp - 0.5f) * 0.9f;
      float island = 1.0f - smoothstep(0.55f, 1.05f, d);

      // Ridged mountains concentrated toward the island core.
      float core = smoothstep(0.60f, 1.0f, island);
      float mountains = noise::ridge(x * 0.008f, z * 0.008f, 4, seed + 13u);

      float h = SEABED + island * (16.0f + base * 24.0f) + core * mountains * 38.0f;
      heights_[j * (GRID + 1) + i] = h;
      minH_ = std::min(minH_, h);
      maxH_ = std::max(maxH_, h);
    }
  }
}

void Terrain::generateBlank(std::uint32_t seed) {
  seed_ = seed;
  heights_.assign((GRID + 1) * (GRID + 1), SEABED);
  for (int j = 0; j <= GRID; ++j) {
    for (int i = 0; i <= GRID; ++i) {
      float x = -SIZE * 0.5f + SIZE * static_cast<float>(i) / GRID;
      float z = -SIZE * 0.5f + SIZE * static_cast<float>(j) / GRID;
      float d = std::sqrt(x * x + z * z) / (SIZE * 0.42f);
      // A flat 5 m plateau with a beach shoulder easing into the sea.
      float island = 1.0f - smoothstep(0.55f, 0.95f, d);
      heights_[j * (GRID + 1) + i] = SEABED + island * (5.0f - SEABED);
    }
  }
  refreshStats();
}

void Terrain::raiseDisc(float cx, float cz, float radius, float amount) {
  if (heights_.empty() || radius <= 0.0f) return;
  int i0, i1, j0, j1;
  discCells(cx, cz, radius, i0, i1, j0, j1);
  for (int j = j0; j <= j1; ++j) {
    for (int i = i0; i <= i1; ++i) {
      float x = -SIZE * 0.5f + SIZE * static_cast<float>(i) / GRID;
      float z = -SIZE * 0.5f + SIZE * static_cast<float>(j) / GRID;
      float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz)) / radius;
      if (d >= 1.0f) continue;
      float w = 1.0f - smoothstep(0.35f, 1.0f, d);
      heights_[j * (GRID + 1) + i] += amount * w;
    }
  }
  refreshStats();
}

void Terrain::smoothDisc(float cx, float cz, float radius, float strength) {
  if (heights_.empty() || radius <= 0.0f) return;
  int i0, i1, j0, j1;
  discCells(cx, cz, radius, i0, i1, j0, j1);
  // Blend toward the 4-neighbor average, sampled from a copy of the touched
  // band so the pass order cannot bias the result.
  std::vector<float> before(heights_);
  auto at = [&](int i, int j) {
    return before[std::clamp(j, 0, GRID) * (GRID + 1) + std::clamp(i, 0, GRID)];
  };
  for (int j = j0; j <= j1; ++j) {
    for (int i = i0; i <= i1; ++i) {
      float x = -SIZE * 0.5f + SIZE * static_cast<float>(i) / GRID;
      float z = -SIZE * 0.5f + SIZE * static_cast<float>(j) / GRID;
      float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz)) / radius;
      if (d >= 1.0f) continue;
      float w = (1.0f - smoothstep(0.35f, 1.0f, d)) * std::min(1.0f, strength);
      float avg = (at(i - 1, j) + at(i + 1, j) + at(i, j - 1) + at(i, j + 1)) * 0.25f;
      float& h = heights_[j * (GRID + 1) + i];
      h += (avg - h) * w;
    }
  }
  refreshStats();
}

bool Terrain::setHeights(const std::vector<float>& h, std::uint32_t seed) {
  if (h.size() != static_cast<std::size_t>((GRID + 1) * (GRID + 1))) return false;
  heights_ = h;
  seed_ = seed;
  refreshStats();
  return true;
}

void Terrain::refreshStats() {
  minH_ = 1e9f;
  maxH_ = -1e9f;
  for (float h : heights_) {
    minH_ = std::min(minH_, h);
    maxH_ = std::max(maxH_, h);
  }
}

void Terrain::discCells(float cx, float cz, float radius, int& i0, int& i1,
                        int& j0, int& j1) const {
  auto toCell = [](float v) {
    return (v + SIZE * 0.5f) / SIZE * GRID;
  };
  i0 = std::clamp(static_cast<int>(std::floor(toCell(cx - radius))), 0, GRID);
  i1 = std::clamp(static_cast<int>(std::ceil(toCell(cx + radius))), 0, GRID);
  j0 = std::clamp(static_cast<int>(std::floor(toCell(cz - radius))), 0, GRID);
  j1 = std::clamp(static_cast<int>(std::ceil(toCell(cz + radius))), 0, GRID);
}

void Terrain::flattenDisc(float cx, float cz, float radius, float targetH,
                          float strength) {
  if (heights_.empty()) return;
  minH_ = 1e9f;
  maxH_ = -1e9f;
  for (int j = 0; j <= GRID; ++j) {
    for (int i = 0; i <= GRID; ++i) {
      float x = -SIZE * 0.5f + SIZE * static_cast<float>(i) / GRID;
      float z = -SIZE * 0.5f + SIZE * static_cast<float>(j) / GRID;
      float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz)) / radius;
      if (d < 1.0f) {
        // Full flattening over the inner half, smooth shoulder to the rim.
        float w = 1.0f - smoothstep(0.5f, 1.0f, d);
        float& h = heights_[j * (GRID + 1) + i];
        h += (targetH - h) * w * strength;
      }
      float h = heights_[j * (GRID + 1) + i];
      minH_ = std::min(minH_, h);
      maxH_ = std::max(maxH_, h);
    }
  }
}

float Terrain::vertexHeight(int i, int j) const {
  i = std::clamp(i, 0, GRID);
  j = std::clamp(j, 0, GRID);
  return heights_[j * (GRID + 1) + i];
}

float Terrain::heightAt(float x, float z) const {
  if (heights_.empty()) return SEABED;
  float gx = (x + SIZE * 0.5f) / SIZE * GRID;
  float gz = (z + SIZE * 0.5f) / SIZE * GRID;
  if (gx < 0.0f || gz < 0.0f || gx > GRID || gz > GRID) return SEABED;

  int i = static_cast<int>(gx);
  int j = static_cast<int>(gz);
  float fx = gx - i;
  float fz = gz - j;

  float h00 = vertexHeight(i, j);
  float h10 = vertexHeight(i + 1, j);
  float h01 = vertexHeight(i, j + 1);
  float h11 = vertexHeight(i + 1, j + 1);

  float h0 = h00 + (h10 - h00) * fx;
  float h1 = h01 + (h11 - h01) * fx;
  return h0 + (h1 - h0) * fz;
}

glm::vec3 Terrain::normalAt(float x, float z) const {
  const float e = SIZE / GRID;
  float hl = heightAt(x - e, z);
  float hr = heightAt(x + e, z);
  float hd = heightAt(x, z - e);
  float hu = heightAt(x, z + e);
  return glm::normalize(glm::vec3(hl - hr, 2.0f * e, hd - hu));
}

bool Terrain::raycast(const glm::vec3& origin, const glm::vec3& dir,
                      glm::vec3& hit, float maxDist) const {
  float t = 0.0f;
  glm::vec3 p = origin;
  float prevT = 0.0f;

  if (origin.y <= heightAt(origin.x, origin.z)) {
    hit = origin;
    return true;
  }

  while (t < maxDist) {
    // Bigger steps when far above the surface, but never step too coarsely.
    float above = p.y - heightAt(p.x, p.z);
    if (above <= 0.0f) {
      // Bisect between prevT and t for a precise hit.
      float lo = prevT, hi = t;
      for (int it = 0; it < 20; ++it) {
        float mid = (lo + hi) * 0.5f;
        glm::vec3 m = origin + dir * mid;
        if (m.y - heightAt(m.x, m.z) > 0.0f)
          lo = mid;
        else
          hi = mid;
      }
      hit = origin + dir * ((lo + hi) * 0.5f);
      return true;
    }
    // Ray climbing away above the tallest terrain can never hit.
    if (dir.y > 0.0f && p.y > maxH_ + 1.0f) return false;

    prevT = t;
    t += std::max(0.35f, above * 0.5f);
    p = origin + dir * t;
  }
  return false;
}

MeshData Terrain::buildMeshData() const {
  MeshData md;
  md.vertices.reserve((GRID + 1) * (GRID + 1) * 9);
  md.indices.reserve(GRID * GRID * 6);

  for (int j = 0; j <= GRID; ++j) {
    for (int i = 0; i <= GRID; ++i) {
      float x = -SIZE * 0.5f + SIZE * static_cast<float>(i) / GRID;
      float z = -SIZE * 0.5f + SIZE * static_cast<float>(j) / GRID;
      float h = vertexHeight(i, j);
      glm::vec3 n = normalAt(x, z);

      // Painterly height/slope palette: seabed, sand, grass, rock, snow.
      float variation = noise::fbm(x * 0.05f, z * 0.05f, 3, seed_ + 5u) - 0.5f;
      glm::vec3 seabed(0.42f, 0.40f, 0.30f);
      glm::vec3 sand(0.80f, 0.72f, 0.52f);
      glm::vec3 grass(0.30f + variation * 0.10f, 0.52f + variation * 0.12f, 0.19f);
      glm::vec3 rock(0.46f, 0.44f, 0.42f);
      glm::vec3 snow(0.92f, 0.93f, 0.95f);

      glm::vec3 c = seabed;
      c = glm::mix(c, sand, smoothstep(-2.5f, -0.5f, h));
      c = glm::mix(c, grass, smoothstep(1.2f, 3.5f, h + variation * 2.0f));
      c = glm::mix(c, rock, smoothstep(31.0f, 39.0f, h + variation * 8.0f));
      c = glm::mix(c, snow, smoothstep(43.0f, 49.0f, h + variation * 4.0f));
      // Steep slopes read as exposed rock regardless of altitude.
      c = glm::mix(rock * (0.9f + variation * 0.3f), c, smoothstep(0.42f, 0.60f, n.y));

      md.addVertex(glm::vec3(x, h, z), n, c);
    }
  }

  for (int j = 0; j < GRID; ++j) {
    for (int i = 0; i < GRID; ++i) {
      std::uint32_t a = j * (GRID + 1) + i;
      std::uint32_t b = a + 1;
      std::uint32_t c = a + (GRID + 1);
      std::uint32_t d = c + 1;
      md.addTriangle(a, c, b);
      md.addTriangle(b, c, d);
    }
  }
  return md;
}

float Terrain::landFraction() const {
  if (heights_.empty()) return 0.0f;
  std::size_t land = 0;
  for (float h : heights_)
    if (h > WATER_LEVEL) ++land;
  return static_cast<float>(land) / static_cast<float>(heights_.size());
}
