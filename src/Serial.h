#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

// Little-endian byte-level serialization helpers (M7): identical bytes on
// every platform, float bit patterns preserved exactly. Sim-side, no GL.
namespace serial {

struct Writer {
  std::vector<std::uint8_t>& out;

  void u8(std::uint8_t v) { out.push_back(v); }
  void b(bool v) { u8(v ? 1 : 0); }
  void u32(std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
  }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void f32(float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, sizeof v);
    u32(v);
  }
  void v2(const glm::vec2& v) {
    f32(v.x);
    f32(v.y);
  }
  void v3(const glm::vec3& v) {
    f32(v.x);
    f32(v.y);
    f32(v.z);
  }
  void quat(const glm::quat& q) {
    f32(q.w);
    f32(q.x);
    f32(q.y);
    f32(q.z);
  }
};

struct Reader {
  const std::uint8_t* p;
  std::size_t n;
  std::size_t off = 0;
  bool ok = true;

  std::uint8_t u8() {
    if (off + 1 > n) {
      ok = false;
      return 0;
    }
    return p[off++];
  }
  bool b() { return u8() != 0; }
  std::uint32_t u32() {
    if (off + 4 > n) {
      ok = false;
      return 0;
    }
    std::uint32_t v = static_cast<std::uint32_t>(p[off]) |
                      static_cast<std::uint32_t>(p[off + 1]) << 8 |
                      static_cast<std::uint32_t>(p[off + 2]) << 16 |
                      static_cast<std::uint32_t>(p[off + 3]) << 24;
    off += 4;
    return v;
  }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  float f32() {
    std::uint32_t v = u32();
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
  }
  glm::vec2 v2() {
    float x = f32(), y = f32();
    return {x, y};
  }
  glm::vec3 v3() {
    float x = f32(), y = f32(), z = f32();
    return {x, y, z};
  }
  glm::quat quat() {
    float w = f32(), x = f32(), y = f32(), z = f32();
    return {w, x, y, z};
  }
};

}  // namespace serial
