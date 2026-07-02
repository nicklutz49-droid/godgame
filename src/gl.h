#pragma once
// Minimal OpenGL 3.3 core loader. Function pointers are resolved through
// SDL_GL_GetProcAddress and live inside the `gl` struct (gl.Enable, gl.Clear...)
// so the symbol names never collide with a system libGL.

#include <cstddef>

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLsizei = int;
using GLubyte = unsigned char;
using GLuint = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

#ifdef _WIN32
#define GLCALL __stdcall
#else
#define GLCALL
#endif

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_MULTISAMPLE 0x809D
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

// X(returnType, NameWithoutGlPrefix, argumentList)
#define GL_FUNC_LIST(X) \
  X(void, Enable, (GLenum)) \
  X(void, Disable, (GLenum)) \
  X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, Clear, (GLbitfield)) \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, DepthFunc, (GLenum)) \
  X(void, DepthMask, (GLboolean)) \
  X(void, BlendFunc, (GLenum, GLenum)) \
  X(void, CullFace, (GLenum)) \
  X(void, FrontFace, (GLenum)) \
  X(void, PolygonMode, (GLenum, GLenum)) \
  X(void, PixelStorei, (GLenum, GLint)) \
  X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)) \
  X(GLenum, GetError, ()) \
  X(const GLubyte*, GetString, (GLenum)) \
  X(void, GenVertexArrays, (GLsizei, GLuint*)) \
  X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
  X(void, BindVertexArray, (GLuint)) \
  X(void, GenBuffers, (GLsizei, GLuint*)) \
  X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
  X(void, BindBuffer, (GLenum, GLuint)) \
  X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
  X(void, EnableVertexAttribArray, (GLuint)) \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
  X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
  X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*)) \
  X(GLuint, CreateShader, (GLenum)) \
  X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
  X(void, CompileShader, (GLuint)) \
  X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
  X(void, DeleteShader, (GLuint)) \
  X(GLuint, CreateProgram, ()) \
  X(void, AttachShader, (GLuint, GLuint)) \
  X(void, LinkProgram, (GLuint)) \
  X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
  X(void, UseProgram, (GLuint)) \
  X(void, DeleteProgram, (GLuint)) \
  X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
  X(void, Uniform1i, (GLint, GLint)) \
  X(void, Uniform1f, (GLint, GLfloat)) \
  X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
  X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
  X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))

struct GLApi {
#define GL_DECLARE_MEMBER(ret, name, args) ret(GLCALL* name) args = nullptr;
  GL_FUNC_LIST(GL_DECLARE_MEMBER)
#undef GL_DECLARE_MEMBER

  // Resolve every pointer; returns false (and logs) if any function is missing.
  bool loadAll();
};

extern GLApi gl;
