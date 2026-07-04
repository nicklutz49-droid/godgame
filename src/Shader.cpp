#include "Shader.h"

#include <SDL.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdlib>
#include <vector>

namespace {

GLuint compileStage(GLenum type, const char* src, const char* debugName) {
  GLuint shader = gl.CreateShader(type);
  gl.ShaderSource(shader, 1, &src, nullptr);
  gl.CompileShader(shader);

  GLint status = GL_FALSE;
  gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    char log[4096];
    GLsizei len = 0;
    gl.GetShaderInfoLog(shader, sizeof(log), &len, log);
    SDL_Log("Shader compile error in %s (%s):\n%.*s", debugName,
            type == GL_VERTEX_SHADER ? "vertex" : "fragment", len, log);
    std::abort();
  }
  return shader;
}

}  // namespace

Shader::~Shader() {
  if (program_) gl.DeleteProgram(program_);
}

void Shader::compile(const char* vertexSrc, const char* fragmentSrc, const char* debugName) {
  GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc, debugName);
  GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, debugName);

  program_ = gl.CreateProgram();
  gl.AttachShader(program_, vs);
  gl.AttachShader(program_, fs);
  gl.LinkProgram(program_);

  GLint status = GL_FALSE;
  gl.GetProgramiv(program_, GL_LINK_STATUS, &status);
  if (!status) {
    char log[4096];
    GLsizei len = 0;
    gl.GetProgramInfoLog(program_, sizeof(log), &len, log);
    SDL_Log("Shader link error in %s:\n%.*s", debugName, len, log);
    std::abort();
  }

  gl.DeleteShader(vs);
  gl.DeleteShader(fs);
}

void Shader::use() const { gl.UseProgram(program_); }

void Shader::set(const char* name, float v) const {
  gl.Uniform1f(gl.GetUniformLocation(program_, name), v);
}
void Shader::set(const char* name, int v) const {
  gl.Uniform1i(gl.GetUniformLocation(program_, name), v);
}
void Shader::set(const char* name, const glm::vec2& v) const {
  gl.Uniform2f(gl.GetUniformLocation(program_, name), v.x, v.y);
}
void Shader::set(const char* name, const glm::vec3& v) const {
  gl.Uniform3f(gl.GetUniformLocation(program_, name), v.x, v.y, v.z);
}
void Shader::set(const char* name, const glm::vec4& v) const {
  gl.Uniform4f(gl.GetUniformLocation(program_, name), v.x, v.y, v.z, v.w);
}
void Shader::set(const char* name, const glm::vec3* v, int count) const {
  gl.Uniform3fv(gl.GetUniformLocation(program_, name), count,
                reinterpret_cast<const GLfloat*>(v));
}
void Shader::set(const char* name, const glm::mat4& m) const {
  gl.UniformMatrix4fv(gl.GetUniformLocation(program_, name), 1, GL_FALSE,
                      glm::value_ptr(m));
}
