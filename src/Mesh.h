#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "gl.h"

// CPU-side mesh under construction. Vertex layout is position(3) normal(3)
// color(3), interleaved. Primitive builders append transformed geometry so a
// whole placeholder model (tree, rock, hand...) becomes one draw call.
struct MeshData {
  std::vector<float> vertices;
  std::vector<std::uint32_t> indices;

  std::uint32_t vertexCount() const {
    return static_cast<std::uint32_t>(vertices.size() / 9);
  }

  void addVertex(const glm::vec3& p, const glm::vec3& n, const glm::vec3& c);
  void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c);

  // All builders transform points by `xform` (normals by its inverse-transpose).
  void addBox(const glm::mat4& xform, const glm::vec3& halfExtents, const glm::vec3& color);
  void addCylinder(const glm::mat4& xform, float radiusBottom, float radiusTop,
                   float height, int segments, const glm::vec3& color);
  // Faceted blob: icosahedron radially jittered by a seeded hash - our rocks.
  void addRock(const glm::mat4& xform, float radius, std::uint32_t seed, const glm::vec3& color);
  // Flat disc facing +Y (used for blob shadows).
  void addDisc(const glm::mat4& xform, float radius, int segments, const glm::vec3& color);
};

class Mesh {
 public:
  Mesh() = default;
  ~Mesh();
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&& other) noexcept;
  Mesh& operator=(Mesh&& other) noexcept;

  void upload(const MeshData& data);
  void draw() const;
  bool valid() const { return vao_ != 0; }

 private:
  void destroy();

  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;
  GLsizei indexCount_ = 0;
};
