#include "Sound.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Noise.h"

// All synthesis is float mono at kRate; the callback mixes voices into the
// device's float stereo stream. Main thread and callback share state under
// SDL_LockAudioDevice - no atomics gymnastics, audio blocks are short.
namespace snd {

namespace {

constexpr int kRate = 44100;
constexpr float kPi = 3.14159265358979f;

std::vector<float> gSfx[static_cast<int>(Sfx::Count)];
std::vector<float> gBed[static_cast<int>(Bed::Count)];
bool gBaked = false;

SDL_AudioDeviceID gDev = 0;
float gMaster = 0.8f;
glm::vec3 gEarPos{0.0f};
glm::vec3 gEarRight{1.0f, 0.0f, 0.0f};

struct Voice {
  const std::vector<float>* buf = nullptr;
  double pos = 0.0;
  double step = 1.0;
  float gainL = 0.0f, gainR = 0.0f;
  // Beds ease toward a live target; one-shots keep their launch gain.
  float targetL = 0.0f, targetR = 0.0f;
  bool loop = false;
  bool active = false;
};
constexpr int kBedVoices = static_cast<int>(Bed::Count);
constexpr int kVoices = kBedVoices + 18;
Voice gVoices[kVoices];

// --- the bakery: tiny helpers, all deterministic ---------------------------

float noiseStep(noise::XorShift& r) { return r.range(-1.0f, 1.0f); }

// One-pole lowpass: alpha in (0,1], smaller = darker.
struct LP {
  float y = 0.0f;
  float alpha;
  explicit LP(float a) : alpha(a) {}
  float operator()(float x) {
    y += alpha * (x - y);
    return y;
  }
};

std::vector<float> synth(float seconds, std::uint32_t seed,
                         float (*gen)(float t, float T, noise::XorShift&, LP&),
                         float lpAlpha = 1.0f) {
  int n = static_cast<int>(seconds * kRate);
  std::vector<float> out(static_cast<std::size_t>(n));
  noise::XorShift rng(seed);
  LP lp(lpAlpha);
  for (int i = 0; i < n; ++i) {
    float t = static_cast<float>(i) / kRate;
    out[static_cast<std::size_t>(i)] = gen(t, seconds, rng, lp);
  }
  // Normalize to a healthy but unclipped level.
  float peak = 1e-6f;
  for (float v : out) peak = std::max(peak, std::abs(v));
  float k = 0.85f / peak;
  for (float& v : out) v *= k;
  return out;
}

// Crossfade the tail into the head so a bed loops without a click.
void loopClean(std::vector<float>& b, float fadeSeconds) {
  int f = std::min(static_cast<int>(fadeSeconds * kRate),
                   static_cast<int>(b.size() / 3));
  for (int i = 0; i < f; ++i) {
    float w = static_cast<float>(i) / f;
    std::size_t tail = b.size() - static_cast<std::size_t>(f) + i;
    b[static_cast<std::size_t>(i)] =
        b[static_cast<std::size_t>(i)] * w + b[tail] * (1.0f - w);
  }
  b.resize(b.size() - static_cast<std::size_t>(f));
}

// --- the mixer --------------------------------------------------------------

void audioCallback(void*, Uint8* stream, int len) {
  float* out = reinterpret_cast<float*>(stream);
  int frames = len / static_cast<int>(2 * sizeof(float));
  std::memset(stream, 0, static_cast<std::size_t>(len));
  for (Voice& v : gVoices) {
    if (!v.active || !v.buf || v.buf->empty()) continue;
    const std::vector<float>& b = *v.buf;
    double n = static_cast<double>(b.size());
    for (int i = 0; i < frames; ++i) {
      if (v.pos >= n) {
        if (v.loop) {
          v.pos -= n;
        } else {
          v.active = false;
          break;
        }
      }
      auto i0 = static_cast<std::size_t>(v.pos);
      std::size_t i1 = i0 + 1 < b.size() ? i0 + 1 : 0;
      float frac = static_cast<float>(v.pos - static_cast<double>(i0));
      float s = b[i0] + (b[i1] - b[i0]) * frac;
      // Beds glide toward their live targets; ~100 ms time constant.
      v.gainL += (v.targetL - v.gainL) * 0.00022f;
      v.gainR += (v.targetR - v.gainR) * 0.00022f;
      out[i * 2 + 0] += s * v.gainL * gMaster;
      out[i * 2 + 1] += s * v.gainR * gMaster;
      v.pos += v.step;
    }
  }
  for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void startVoice(const std::vector<float>& buf, float gl, float gr, float pitch,
                bool loop, int fixedSlot = -1) {
  if (!gDev) return;
  SDL_LockAudioDevice(gDev);
  Voice* pick = nullptr;
  if (fixedSlot >= 0) {
    pick = &gVoices[fixedSlot];
  } else {
    float quietest = 1e9f;
    for (int i = kBedVoices; i < kVoices; ++i) {
      Voice& v = gVoices[i];
      if (!v.active) {
        pick = &v;
        break;
      }
      float g = v.gainL + v.gainR;
      if (g < quietest) {
        quietest = g;
        pick = &v;
      }
    }
  }
  if (pick) {
    pick->buf = &buf;
    pick->pos = 0.0;
    pick->step = static_cast<double>(std::clamp(pitch, 0.25f, 4.0f));
    pick->gainL = pick->targetL = gl;
    pick->gainR = pick->targetR = gr;
    pick->loop = loop;
    pick->active = true;
  }
  SDL_UnlockAudioDevice(gDev);
}

}  // namespace

// --- the bank ---------------------------------------------------------------

void bake(bool force) {
  if (gBaked && !force) return;
  gBaked = true;
  auto S = [](Sfx s) -> std::vector<float>& { return gSfx[static_cast<int>(s)]; };
  auto B = [](Bed b) -> std::vector<float>& { return gBed[static_cast<int>(b)]; };

  S(Sfx::ThudSoft) = synth(0.10f, 11u, [](float t, float, noise::XorShift& r, LP& lp) {
    return lp(noiseStep(r)) * std::exp(-t * 45.0f) +
           0.55f * std::sin(2 * kPi * 55.0f * t) * std::exp(-t * 30.0f);
  }, 0.08f);
  S(Sfx::ThudHard) = synth(0.18f, 12u, [](float t, float, noise::XorShift& r, LP& lp) {
    return lp(noiseStep(r)) * std::exp(-t * 26.0f) +
           0.8f * std::sin(2 * kPi * 44.0f * t) * std::exp(-t * 16.0f);
  }, 0.06f);
  S(Sfx::Splash) = synth(0.55f, 13u, [](float t, float, noise::XorShift& r, LP& lp) {
    float fizz = (noiseStep(r) - lp(noiseStep(r))) * std::exp(-t * 7.0f);
    float plop = std::sin(2 * kPi * (300.0f - 4000.0f * std::min(t, 0.05f)) * t) *
                 std::exp(-t * 40.0f);
    return fizz * 0.8f + plop * 0.7f;
  }, 0.35f);
  S(Sfx::Chop) = synth(0.06f, 14u, [](float t, float, noise::XorShift& r, LP& lp) {
    return lp(noiseStep(r)) * std::exp(-t * 110.0f) +
           0.6f * std::sin(2 * kPi * 180.0f * t) * std::exp(-t * 90.0f);
  }, 0.30f);
  S(Sfx::TreeFall) = synth(0.45f, 15u, [](float t, float, noise::XorShift& r, LP& lp) {
    float crack = t < 0.012f ? noiseStep(r) : 0.0f;
    return lp(noiseStep(r)) * std::exp(-t * 9.0f) + crack * 0.9f;
  }, 0.05f);
  S(Sfx::Scream) = synth(0.45f, 16u, [](float t, float T, noise::XorShift& r, LP&) {
    float f = 750.0f - 370.0f * (t / T) + 45.0f * std::sin(2 * kPi * 6.0f * t);
    float saw = 2.0f * (f * t - std::floor(f * t)) - 1.0f;
    float env = std::min(t * 90.0f, 1.0f) * std::exp(-t * 6.0f);
    return (saw * 0.8f + noiseStep(r) * 0.2f) * env;
  });
  S(Sfx::Whoosh) = synth(0.30f, 17u, [](float t, float T, noise::XorShift& r, LP& lp) {
    lp.alpha = 0.02f + 0.25f * (t / T);
    return lp(noiseStep(r)) * std::sin(kPi * t / T);
  }, 0.02f);
  S(Sfx::Place) = synth(0.05f, 18u, [](float t, float, noise::XorShift& r, LP& lp) {
    return lp(noiseStep(r)) * std::exp(-t * 90.0f);
  }, 0.18f);
  S(Sfx::Clack) = synth(0.09f, 19u, [](float t, float, noise::XorShift& r, LP& lp) {
    float k1 = std::sin(2 * kPi * 240.0f * t) * std::exp(-t * 70.0f);
    float t2 = t - 0.035f;
    float k2 = t2 > 0.0f ? std::sin(2 * kPi * 300.0f * t2) * std::exp(-t2 * 70.0f) : 0.0f;
    return k1 + 0.8f * k2 + lp(noiseStep(r)) * std::exp(-t * 80.0f) * 0.5f;
  }, 0.25f);
  S(Sfx::Complete) = synth(0.9f, 20u, [](float t, float, noise::XorShift&, LP&) {
    return std::sin(2 * kPi * 660.0f * t) * std::exp(-t * 4.0f) +
           0.5f * std::sin(2 * kPi * 990.0f * t) * std::exp(-t * 5.0f) +
           0.3f * std::sin(2 * kPi * 1320.0f * t) * std::exp(-t * 6.0f);
  });
  S(Sfx::Cast) = synth(0.6f, 21u, [](float t, float, noise::XorShift&, LP&) {
    float a = 0.0f;
    const float starts[4] = {0.0f, 0.09f, 0.19f, 0.30f};
    const float freqs[4] = {880.0f, 1108.0f, 1318.0f, 1760.0f};
    for (int k = 0; k < 4; ++k) {
      float tt = t - starts[k];
      if (tt > 0.0f) a += std::sin(2 * kPi * freqs[k] * tt) * std::exp(-tt * 9.0f);
    }
    return a;
  });
  S(Sfx::Bloom) = synth(0.7f, 22u, [](float t, float T, noise::XorShift&, LP&) {
    float sw = std::sin(kPi * std::min(t / (T * 0.55f), 1.0f));
    return sw * (std::sin(2 * kPi * 330.0f * t) + 0.7f * std::sin(2 * kPi * 415.3f * t) +
                 0.5f * std::sin(2 * kPi * 494.0f * t));
  });
  S(Sfx::Explosion) = synth(0.9f, 23u, [](float t, float, noise::XorShift& r, LP& lp) {
    float crack = t < 0.02f ? noiseStep(r) * 1.2f : 0.0f;
    return lp(noiseStep(r)) * std::exp(-t * 5.0f) +
           0.9f * std::sin(2 * kPi * 40.0f * t) * std::exp(-t * 6.0f) + crack;
  }, 0.045f);
  S(Sfx::Rumble) = synth(1.6f, 24u, [](float t, float, noise::XorShift& r, LP& lp) {
    float env = std::min(t * 10.0f, 1.0f) * std::exp(-t * 2.2f);
    return (lp(noiseStep(r)) + 0.5f * std::sin(2 * kPi * 30.0f * t)) * env;
  }, 0.03f);
  S(Sfx::Bell) = synth(1.2f, 25u, [](float t, float, noise::XorShift&, LP&) {
    return std::sin(2 * kPi * 520.0f * t) * std::exp(-t * 2.5f) +
           0.6f * std::sin(2 * kPi * 1040.0f * t) * std::exp(-t * 4.0f) +
           0.4f * std::sin(2 * kPi * 1435.0f * t) * std::exp(-t * 6.0f);
  });
  S(Sfx::UiMove) = synth(0.045f, 26u, [](float t, float T, noise::XorShift&, LP&) {
    return std::sin(2 * kPi * 700.0f * t) * std::sin(kPi * t / T);
  });
  S(Sfx::UiSelect) = synth(0.09f, 27u, [](float t, float T, noise::XorShift&, LP&) {
    float f = t < 0.045f ? 700.0f : 1050.0f;
    return std::sin(2 * kPi * f * t) * std::sin(kPi * t / T);
  });
  S(Sfx::UiDeny) = synth(0.12f, 28u, [](float t, float T, noise::XorShift&, LP&) {
    float sq = std::sin(2 * kPi * 220.0f * t) > 0.0f ? 1.0f : -1.0f;
    return sq * 0.6f * std::sin(kPi * t / T);
  });

  B(Bed::Wind) = synth(5.0f, 40u, [](float t, float, noise::XorShift& r, LP& lp) {
    return lp(noiseStep(r)) * (0.75f + 0.25f * std::sin(2 * kPi * 0.17f * t));
  }, 0.015f);
  loopClean(B(Bed::Wind), 0.6f);
  B(Bed::Birds) = synth(8.0f, 41u, [](float t, float, noise::XorShift& r, LP&) {
    // Sparse chirps: each 0.5 s window may host one, deterministically.
    int win = static_cast<int>(t * 2.0f);
    noise::XorShift w(static_cast<std::uint32_t>(win) * 2654435761u + 41u);
    if (w.uniform() > 0.42f) return 0.0f;
    float start = w.range(0.02f, 0.30f);
    float tt = t - (static_cast<float>(win) * 0.5f + start);
    float dur = w.range(0.06f, 0.13f);
    if (tt < 0.0f || tt > dur) return 0.0f;
    float f = w.range(2100.0f, 3600.0f) + w.range(600.0f, 1400.0f) * (tt / dur);
    (void)r;
    return std::sin(2 * kPi * f * tt) * std::sin(kPi * tt / dur) * 0.8f;
  });
  loopClean(B(Bed::Birds), 0.4f);
  B(Bed::Crickets) = synth(4.0f, 42u, [](float t, float, noise::XorShift&, LP&) {
    float gate = std::sin(2 * kPi * 2.4f * t) > 0.15f ? 1.0f : 0.0f;
    float pulse = std::sin(2 * kPi * 31.0f * t) > 0.6f ? 1.0f : 0.0f;
    return std::sin(2 * kPi * 4300.0f * t) * gate * pulse * 0.6f;
  });
  loopClean(B(Bed::Crickets), 0.3f);
  B(Bed::Fire) = synth(4.0f, 43u, [](float t, float, noise::XorShift& r, LP& lp) {
    float hiss = (noiseStep(r) - lp(noiseStep(r))) * 0.22f;
    int win = static_cast<int>(t * 24.0f);
    noise::XorShift w(static_cast<std::uint32_t>(win) * 747796405u + 43u);
    float tt = t - static_cast<float>(win) / 24.0f;
    float pop = (w.uniform() < 0.4f && tt < 0.006f) ? noiseStep(r) * 1.3f : 0.0f;
    return hiss + pop;
  }, 0.5f);
  loopClean(B(Bed::Fire), 0.3f);
  B(Bed::RainBed) = synth(4.0f, 44u, [](float t, float, noise::XorShift& r, LP& lp) {
    return lp(noiseStep(r)) * (0.9f + 0.1f * std::sin(2 * kPi * 0.4f * t));
  }, 0.16f);
  loopClean(B(Bed::RainBed), 0.4f);
  B(Bed::Chant) = synth(4.8f, 45u, [](float t, float, noise::XorShift&, LP&) {
    float a = 0.0f;
    const float fs[3] = {110.0f, 138.6f, 164.8f};
    for (int k = 0; k < 3; ++k) {
      float lfo = 0.75f + 0.25f * std::sin(2 * kPi * 0.21f * t + k * 2.1f);
      float f = fs[k] * (1.0f + 0.004f * std::sin(2 * kPi * 0.13f * t + k));
      float tri = 2.0f * std::abs(2.0f * (f * t - std::floor(f * t + 0.5f))) - 1.0f;
      a += tri * lfo;
    }
    return a;
  });
  loopClean(B(Bed::Chant), 0.6f);
}

std::uint64_t bankChecksum() {
  bake();
  std::uint64_t h = 1469598103934665603ull;
  auto mix = [&](const std::vector<float>& b) {
    for (float v : b) {
      std::uint32_t bits;
      std::memcpy(&bits, &v, sizeof bits);
      h ^= bits;
      h *= 1099511628211ull;
    }
  };
  for (const auto& b : gSfx) mix(b);
  for (const auto& b : gBed) mix(b);
  return h;
}

// --- the device --------------------------------------------------------------

bool init() {
  if (gDev) return true;
  bake();
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
  SDL_AudioSpec want{};
  want.freq = kRate;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = audioCallback;
  SDL_AudioSpec have{};
  gDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!gDev) {
    SDL_Log("No audio device - the island stays silent");
    return false;
  }
  // The beds hold their fixed voices forever, silent until targeted.
  for (int b = 0; b < kBedVoices; ++b) {
    Voice& v = gVoices[b];
    v.buf = &gBed[b];
    v.pos = 0.0;
    v.step = 1.0;
    v.loop = true;
    v.active = true;
    v.gainL = v.gainR = v.targetL = v.targetR = 0.0f;
  }
  SDL_PauseAudioDevice(gDev, 0);
  return true;
}

void shutdown() {
  if (!gDev) return;
  SDL_CloseAudioDevice(gDev);
  gDev = 0;
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void setMasterVolume(float v) {
  float clamped = std::clamp(v, 0.0f, 1.0f);
  if (gDev) SDL_LockAudioDevice(gDev);
  gMaster = clamped;
  if (gDev) SDL_UnlockAudioDevice(gDev);
}
float masterVolume() { return gMaster; }

void play(Sfx s, float gain, float pitch) {
  if (!gDev) return;
  const std::vector<float>& b = gSfx[static_cast<int>(s)];
  startVoice(b, gain * 0.71f, gain * 0.71f, pitch, false);
}

void playAt(Sfx s, const glm::vec3& worldPos, float gain, float pitch) {
  if (!gDev) return;
  glm::vec3 to = worldPos - gEarPos;
  float d = glm::length(to);
  float att = 1.0f / (1.0f + (d / 34.0f) * (d / 34.0f));
  if (att * gain < 0.02f) return;  // out of earshot
  float pan = 0.0f;
  if (d > 1.0f)
    pan = std::clamp(glm::dot(glm::vec2(to.x, to.z) / d,
                              glm::vec2(gEarRight.x, gEarRight.z)),
                     -1.0f, 1.0f) *
          0.6f;
  float gl = std::sqrt((1.0f - pan) * 0.5f);
  float gr = std::sqrt((1.0f + pan) * 0.5f);
  startVoice(gSfx[static_cast<int>(s)], gain * att * gl, gain * att * gr, pitch,
             false);
}

void bed(Bed b, float targetGain) {
  if (!gDev) return;
  Voice& v = gVoices[static_cast<int>(b)];
  float t = std::clamp(targetGain, 0.0f, 1.0f) * 0.5f;  // beds sit low
  SDL_LockAudioDevice(gDev);
  v.targetL = v.targetR = t;
  SDL_UnlockAudioDevice(gDev);
}

void setListener(const glm::vec3& pos, const glm::vec3& forward) {
  if (!gDev) return;
  SDL_LockAudioDevice(gDev);
  gEarPos = pos;
  glm::vec3 f = glm::length(forward) > 1e-4f ? glm::normalize(forward)
                                             : glm::vec3(0.0f, 0.0f, 1.0f);
  gEarRight = glm::normalize(glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f)));
  SDL_UnlockAudioDevice(gDev);
}

}  // namespace snd
