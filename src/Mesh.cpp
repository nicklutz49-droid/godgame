#include "Mesh.h"

#include <utility>

// The GL half of Mesh: upload/draw/lifecycle. Render-side only (calls gl.*),
// so it stays out of the engine-free godgame_sim library. The CPU MeshData
// builders live in MeshData.cpp.

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& other) noexcept { *this = std::move(other); }

Mesh& Mesh::operator=(Mesh&& other) noexcept {
  if (this != &other) {
    destroy();
    vao_ = other.vao_;
    vbo_ = other.vbo_;
    ebo_ = other.ebo_;
    indexCount_ = other.indexCount_;
    other.vao_ = other.vbo_ = other.ebo_ = 0;
    other.indexCount_ = 0;
  }
  return *this;
}

void Mesh::destroy() {
  if (ebo_) gl.DeleteBuffers(1, &ebo_);
  if (vbo_) gl.DeleteBuffers(1, &vbo_);
  if (vao_) gl.DeleteVertexArrays(1, &vao_);
  vao_ = vbo_ = ebo_ = 0;
  indexCount_ = 0;
}

void Mesh::upload(const MeshData& data) {
  destroy();
  gl.GenVertexArrays(1, &vao_);
  gl.BindVertexArray(vao_);

  gl.GenBuffers(1, &vbo_);
  gl.BindBuffer(GL_ARRAY_BUFFER, vbo_);
  gl.BufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(data.vertices.size() * sizeof(float)),
                data.vertices.data(), GL_STATIC_DRAW);

  gl.GenBuffers(1, &ebo_);
  gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(data.indices.size() * sizeof(std::uint32_t)),
                data.indices.data(), GL_STATIC_DRAW);

  const GLsizei stride = 9 * sizeof(float);
  gl.EnableVertexAttribArray(0);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
  gl.EnableVertexAttribArray(1);
  gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                         reinterpret_cast<void*>(3 * sizeof(float)));
  gl.EnableVertexAttribArray(2);
  gl.VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                         reinterpret_cast<void*>(6 * sizeof(float)));

  gl.BindVertexArray(0);
  indexCount_ = static_cast<GLsizei>(data.indices.size());
}

void Mesh::draw() const {
  if (!vao_ || indexCount_ == 0) return;
  gl.BindVertexArray(vao_);
  gl.DrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
  gl.BindVertexArray(0);
}
