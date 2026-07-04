#pragma once

#include <glm/glm.hpp>
#include <string>

#include "gl.h"

class Shader {
 public:
  Shader() = default;
  ~Shader();
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  // Compiles and links; aborts with a log message on error (placeholder
  // shaders are compiled in, so a failure here is always a programming bug).
  void compile(const char* vertexSrc, const char* fragmentSrc, const char* debugName);

  void use() const;

  void set(const char* name, float v) const;
  void set(const char* name, int v) const;
  void set(const char* name, const glm::vec2& v) const;
  void set(const char* name, const glm::vec3& v) const;
  void set(const char* name, const glm::vec4& v) const;
  void set(const char* name, const glm::vec3* v, int count) const;
  void set(const char* name, const glm::mat4& m) const;

 private:
  GLuint program_ = 0;
};
