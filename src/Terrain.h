#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "Mesh.h"

// Procedural island heightfield. Placeholder for loading a real Black & White
// .lnd landscape later - the public interface (heightAt/normalAt/raycast) is
// what the rest of the game codes against, so swapping the data source in
// shouldn't disturb anything else.
class Terrain {
 public:
  static constexpr int GRID = 256;          // quads per side
  static constexpr float SIZE = 512.0f;     // world units per side
  static constexpr float WATER_LEVEL = 0.0f;
  static constexpr float SEABED = -14.0f;

  void generate(std::uint32_t seed);

  float heightAt(float x, float z) const;   // bilinear; seabed outside bounds
  glm::vec3 normalAt(float x, float z) const;

  // March the ray against the heightfield. Returns false if it never hits.
  bool raycast(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& hit,
               float maxDist = 1500.0f) const;

  MeshData buildMeshData() const;

  std::uint32_t seed() const { return seed_; }

  // Fraction of vertices above water - used by the headless self-test.
  float landFraction() const;
  float minHeight() const { return minH_; }
  float maxHeight() const { return maxH_; }

 private:
  float vertexHeight(int i, int j) const;

  std::vector<float> heights_;  // (GRID+1)^2, row-major
  std::uint32_t seed_ = 0;
  float minH_ = 0.0f;
  float maxH_ = 0.0f;
};
