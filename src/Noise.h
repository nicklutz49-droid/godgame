#pragma once

#include <cmath>
#include <cstdint>

// Deterministic 2D value noise + fBm. Integer hashing keeps results identical
// across platforms, which matters because terrain generation must match
// whatever seed the player shares.
namespace noise {

// Deterministic atan2 for FOUNDING code (yaws written into world/map/save
// state): fixed-order IEEE mul/add/div only, so results are bit-identical
// across platforms - libm's atan2 is not. Max error ~3e-4 rad (invisible
// for a facing). Same argument convention as std::atan2(y, x).
inline float atan2det(float y, float x) {
  float ax = x < 0.0f ? -x : x;
  float ay = y < 0.0f ? -y : y;
  float mx = ax > ay ? ax : ay;
  float mn = ax > ay ? ay : ax;
  float z = mx > 0.0f ? mn / mx : 0.0f;  // [0, 1]
  float z2 = z * z;
  float a = ((-0.0464964749f * z2 + 0.15931422f) * z2 - 0.327622764f) * z2 * z + z;
  if (ay > ax) a = 1.57079637f - a;
  if (x < 0.0f) a = 3.14159274f - a;
  return y < 0.0f ? -a : a;
}

// Tiny deterministic RNG - the only random source the simulation may use.
struct XorShift {
  std::uint32_t state;
  explicit XorShift(std::uint32_t seed) : state(seed ? seed : 0xBADC0FFEu) {}
  std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  float uniform() { return static_cast<float>(next()) / 4294967295.0f; }
  float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
};

inline float hash(int x, int y, std::uint32_t seed) {
  std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                    static_cast<std::uint32_t>(y) * 668265263u + seed * 2654435761u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h) / 4294967295.0f;  // [0, 1]
}

inline float smooth(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline float value(float x, float y, std::uint32_t seed) {
  int xi = static_cast<int>(std::floor(x));
  int yi = static_cast<int>(std::floor(y));
  float xf = x - static_cast<float>(xi);
  float yf = y - static_cast<float>(yi);
  float u = smooth(xf);
  float v = smooth(yf);

  float a = hash(xi, yi, seed);
  float b = hash(xi + 1, yi, seed);
  float c = hash(xi, yi + 1, seed);
  float d = hash(xi + 1, yi + 1, seed);

  float ab = a + (b - a) * u;
  float cd = c + (d - c) * u;
  return ab + (cd - ab) * v;  // [0, 1]
}

inline float fbm(float x, float y, int octaves, std::uint32_t seed,
                 float lacunarity = 2.0f, float gain = 0.5f) {
  float sum = 0.0f;
  float amp = 0.5f;
  float freq = 1.0f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * value(x * freq, y * freq, seed + static_cast<std::uint32_t>(i) * 101u);
    norm += amp;
    freq *= lacunarity;
    amp *= gain;
  }
  return sum / norm;  // [0, 1]
}

// Ridged variant - sharp crests for mountain ranges.
inline float ridge(float x, float y, int octaves, std::uint32_t seed) {
  float sum = 0.0f;
  float amp = 0.5f;
  float freq = 1.0f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    float n = value(x * freq, y * freq, seed + static_cast<std::uint32_t>(i) * 131u);
    n = 1.0f - std::fabs(2.0f * n - 1.0f);  // fold into a ridge
    sum += amp * n * n;
    norm += amp;
    freq *= 2.0f;
    amp *= 0.5f;
  }
  return sum / norm;  // [0, 1]
}

}  // namespace noise
