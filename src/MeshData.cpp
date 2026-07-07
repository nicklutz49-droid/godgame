#include "Mesh.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

// CPU-side procedural geometry builders. GL-free by design: this lives in the
// engine-free godgame_sim library so Terrain (and the sim's own model data)
// can build meshes without pulling in the GL Mesh class. The GL upload/draw
// half lives in Mesh.cpp, render-side.

namespace {

glm::vec3 transformPoint(const glm::mat4& m, const glm::vec3& p) {
  glm::vec4 r = m * glm::vec4(p, 1.0f);
  return glm::vec3(r);
}

glm::vec3 transformNormal(const glm::mat3& normalMat, const glm::vec3& n) {
  return glm::normalize(normalMat * n);
}

float hash1(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return static_cast<float>(x) / 4294967295.0f;
}

}  // namespace

void MeshData::addVertex(const glm::vec3& p, const glm::vec3& n, const glm::vec3& c) {
  vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z});
}

void MeshData::addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  indices.insert(indices.end(), {a, b, c});
}

void MeshData::addBox(const glm::mat4& xform, const glm::vec3& h, const glm::vec3& color) {
  glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xform)));
  static const glm::vec3 faceNormals[6] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  // Two axes spanning each face, so corners wind counter-clockwise seen from outside.
  static const glm::vec3 faceU[6] = {
      {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
  static const glm::vec3 faceV[6] = {
      {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

  for (int f = 0; f < 6; ++f) {
    glm::vec3 n = faceNormals[f];
    glm::vec3 u = faceU[f] * h;
    glm::vec3 v = faceV[f] * h;
    glm::vec3 c = n * h;
    std::uint32_t base = vertexCount();
    glm::vec3 wn = transformNormal(nm, n);
    addVertex(transformPoint(xform, c - u - v), wn, color);
    addVertex(transformPoint(xform, c + u - v), wn, color);
    addVertex(transformPoint(xform, c + u + v), wn, color);
    addVertex(transformPoint(xform, c - u + v), wn, color);
    addTriangle(base, base + 1, base + 2);
    addTriangle(base, base + 2, base + 3);
  }
}

void MeshData::addCylinder(const glm::mat4& xform, float rBottom, float rTop,
                           float height, int segments, const glm::vec3& color) {
  glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xform)));
  float half = height * 0.5f;
  float twoPi = glm::two_pi<float>();

  for (int i = 0; i < segments; ++i) {
    float a0 = twoPi * static_cast<float>(i) / segments;
    float a1 = twoPi * static_cast<float>(i + 1) / segments;
    glm::vec3 d0(std::cos(a0), 0.0f, std::sin(a0));
    glm::vec3 d1(std::cos(a1), 0.0f, std::sin(a1));

    glm::vec3 b0 = d0 * rBottom + glm::vec3(0, -half, 0);
    glm::vec3 b1 = d1 * rBottom + glm::vec3(0, -half, 0);
    glm::vec3 t0 = d0 * rTop + glm::vec3(0, half, 0);
    glm::vec3 t1 = d1 * rTop + glm::vec3(0, half, 0);

    // Flat-shaded side quad (or triangle when the top radius is zero).
    glm::vec3 mid = glm::normalize(d0 + d1);
    float slope = (rBottom - rTop) / height;
    glm::vec3 n = transformNormal(nm, glm::normalize(glm::vec3(mid.x, slope, mid.z)));

    std::uint32_t base = vertexCount();
    if (rTop > 0.0001f) {
      addVertex(transformPoint(xform, b0), n, color);
      addVertex(transformPoint(xform, t0), n, color);
      addVertex(transformPoint(xform, t1), n, color);
      addVertex(transformPoint(xform, b1), n, color);
      addTriangle(base, base + 1, base + 2);
      addTriangle(base, base + 2, base + 3);
    } else {
      glm::vec3 apex(0, half, 0);
      addVertex(transformPoint(xform, b0), n, color);
      addVertex(transformPoint(xform, apex), n, color);
      addVertex(transformPoint(xform, b1), n, color);
      addTriangle(base, base + 1, base + 2);
    }

    // Bottom cap.
    glm::vec3 down = transformNormal(nm, glm::vec3(0, -1, 0));
    std::uint32_t capBase = vertexCount();
    addVertex(transformPoint(xform, glm::vec3(0, -half, 0)), down, color);
    addVertex(transformPoint(xform, b0), down, color);
    addVertex(transformPoint(xform, b1), down, color);
    addTriangle(capBase, capBase + 1, capBase + 2);

    // Top cap.
    if (rTop > 0.0001f) {
      glm::vec3 up = transformNormal(nm, glm::vec3(0, 1, 0));
      std::uint32_t topBase = vertexCount();
      addVertex(transformPoint(xform, glm::vec3(0, half, 0)), up, color);
      addVertex(transformPoint(xform, t1), up, color);
      addVertex(transformPoint(xform, t0), up, color);
      addTriangle(topBase, topBase + 1, topBase + 2);
    }
  }
}

void MeshData::addRock(const glm::mat4& xform, float radius, std::uint32_t seed,
                       const glm::vec3& color) {
  // Icosahedron vertices.
  const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
  glm::vec3 base[12] = {
      {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
      {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
      {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  static const int faces[20][3] = {
      {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
      {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
      {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
      {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};

  glm::vec3 jittered[12];
  for (int i = 0; i < 12; ++i) {
    float r = radius * (0.72f + 0.55f * hash1(seed * 12u + static_cast<std::uint32_t>(i)));
    glm::vec3 v = glm::normalize(base[i]) * r;
    v.y *= 0.85f;  // rocks sit a little squat
    jittered[i] = v;
  }

  glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xform)));
  for (auto& f : faces) {
    glm::vec3 a = jittered[f[0]], b = jittered[f[1]], c = jittered[f[2]];
    glm::vec3 n = transformNormal(nm, glm::normalize(glm::cross(b - a, c - a)));
    // Slight per-face tint variation sells the faceted-rock look.
    float shade = 0.9f + 0.2f * hash1(seed ^ (static_cast<std::uint32_t>(f[0]) * 733u +
                                              static_cast<std::uint32_t>(f[1]) * 97u));
    std::uint32_t basev = vertexCount();
    addVertex(transformPoint(xform, a), n, color * shade);
    addVertex(transformPoint(xform, b), n, color * shade);
    addVertex(transformPoint(xform, c), n, color * shade);
    addTriangle(basev, basev + 1, basev + 2);
  }
}

void MeshData::addDisc(const glm::mat4& xform, float radius, int segments,
                       const glm::vec3& color) {
  glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xform)));
  glm::vec3 up = transformNormal(nm, glm::vec3(0, 1, 0));
  float twoPi = glm::two_pi<float>();
  std::uint32_t center = vertexCount();
  addVertex(transformPoint(xform, glm::vec3(0)), up, color);
  for (int i = 0; i <= segments; ++i) {
    float a = twoPi * static_cast<float>(i) / segments;
    addVertex(transformPoint(xform, glm::vec3(std::cos(a) * radius, 0, std::sin(a) * radius)),
              up, color);
  }
  for (int i = 0; i < segments; ++i) {
    addTriangle(center, center + 2 + static_cast<std::uint32_t>(i),
                center + 1 + static_cast<std::uint32_t>(i));
  }
}
