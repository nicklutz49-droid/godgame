#pragma once

#include <glm/glm.hpp>

#include <cmath>

#include "Tuning.h"

// Time of day. Owned by World (the sim schedule needs it, so it must exist
// headless). The sim only consults t / daylight() / isNight() - piecewise
// smoothstep, no libm in decision paths. sunDir()/color curves are for the
// renderer.
struct DayCycle {
  float t = 0.35f;  // day fraction; 0.25 = dawn, 0.5 = noon, 0.75 = dusk
  int day = 1;      // counts the midnights (the HUD's "DAY N")
  float secondsPerDay = tune::kSecondsPerDay;

  void advance(float dt) {
    t += dt / secondsPerDay;
    float wraps = std::floor(t);
    t -= wraps;
    day += static_cast<int>(wraps);
  }

  static float smoothstep(float e0, float e1, float x) {
    float u = (x - e0) / (e1 - e0);
    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    return u * u * (3.0f - 2.0f * u);
  }

  // 0 at night, 1 in full day, smooth ramps around dawn/dusk.
  float daylight() const {
    return std::min(smoothstep(0.20f, 0.30f, t), 1.0f - smoothstep(0.70f, 0.80f, t));
  }

  bool isNight() const { return t >= tune::kNightT || t < tune::kDawnT - 0.02f; }

  // --- render-side curves ---

  glm::vec3 sunDir() const {
    float az = 3.14159265f * (t - 0.25f) / 0.5f;  // east -> west over the day
    float elev = std::sin(az) * 1.15f;            // peak ~66 degrees
    float ce = std::cos(elev), se = std::sin(elev);
    return glm::normalize(glm::vec3(std::cos(az) * ce, se, 0.30f * ce + 0.12f));
  }

  glm::vec3 sunColor() const {
    float se = sunDir().y;
    float up = smoothstep(-0.04f, 0.10f, se);
    glm::vec3 noon(0.95f, 0.91f, 0.84f);  // matches the original constant look
    glm::vec3 horizon(1.05f, 0.55f, 0.30f);
    return glm::mix(horizon, noon, smoothstep(0.05f, 0.45f, se)) * up;
  }

  glm::vec3 ambient() const {
    float d = daylight();
    glm::vec3 night(0.11f, 0.13f, 0.21f);
    glm::vec3 day(0.40f, 0.40f, 0.41f);
    return glm::mix(night, day, d);
  }

  glm::vec3 fogColor() const {
    float d = daylight();
    glm::vec3 night(0.05f, 0.07f, 0.13f);
    glm::vec3 day(0.74f, 0.82f, 0.88f);
    glm::vec3 base = glm::mix(night, day, d);
    float duskGlow = d * (1.0f - d) * 4.0f;  // peaks mid-transition
    return glm::mix(base, glm::vec3(0.86f, 0.60f, 0.46f), duskGlow * 0.40f);
  }

  glm::vec3 zenithColor() const {
    return glm::mix(glm::vec3(0.015f, 0.03f, 0.09f), glm::vec3(0.28f, 0.50f, 0.76f),
                    daylight());
  }
};
