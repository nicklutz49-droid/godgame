#include "Water.h"

namespace {

const char* kVertexSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uVP;
uniform float uTime;
out vec3 vWorld;
out vec3 vNormal;
void main() {
  vec3 p = aPos;
  float w1 = sin(p.x * 0.060 + uTime * 1.1) * 0.30;
  float w2 = sin(p.x * 0.030 + p.z * 0.055 + uTime * 0.7) * 0.40;
  float w3 = sin(p.z * 0.045 - uTime * 0.9) * 0.25;
  p.y += w1 + w2 + w3;

  float dx = 0.060 * cos(p.x * 0.060 + uTime * 1.1) * 0.30 +
             0.030 * cos(p.x * 0.030 + p.z * 0.055 + uTime * 0.7) * 0.40;
  float dz = 0.055 * cos(p.x * 0.030 + p.z * 0.055 + uTime * 0.7) * 0.40 +
             0.045 * cos(p.z * 0.045 - uTime * 0.9) * 0.25;
  vNormal = normalize(vec3(-dx, 1.0, -dz));
  vWorld = p;
  gl_Position = uVP * vec4(p, 1.0);
}
)GLSL";

const char* kFragmentSrc = R"GLSL(
#version 330 core
in vec3 vWorld;
in vec3 vNormal;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 FragColor;
void main() {
  vec3 n = normalize(vNormal);
  vec3 v = normalize(uCamPos - vWorld);
  float fresnel = pow(1.0 - max(dot(v, n), 0.0), 3.0);

  vec3 deep = vec3(0.07, 0.29, 0.42);
  vec3 col = mix(deep, uFogColor * 0.9, fresnel * 0.7);

  float spec = pow(max(dot(reflect(-uSunDir, n), v), 0.0), 120.0) * 0.8;
  col += vec3(spec);

  float dist = length(uCamPos - vWorld);
  float fog = 1.0 - exp(-uFogDensity * dist);
  col = mix(col, uFogColor, fog);

  FragColor = vec4(col, 0.66 + fresnel * 0.22);
}
)GLSL";

}  // namespace

void Water::init() {
  shader_.compile(kVertexSrc, kFragmentSrc, "water");

  // Coarse grid; the vertex shader adds the swell. Extends past the far clip
  // plane so the plane's edge can never be seen - the horizon is pure fog.
  const int kSegments = 160;
  const float kExtent = 5200.0f;
  MeshData md;
  for (int j = 0; j <= kSegments; ++j) {
    for (int i = 0; i <= kSegments; ++i) {
      float x = -kExtent * 0.5f + kExtent * static_cast<float>(i) / kSegments;
      float z = -kExtent * 0.5f + kExtent * static_cast<float>(j) / kSegments;
      md.addVertex(glm::vec3(x, 0.0f, z), glm::vec3(0, 1, 0), glm::vec3(0.1f, 0.3f, 0.4f));
    }
  }
  for (int j = 0; j < kSegments; ++j) {
    for (int i = 0; i < kSegments; ++i) {
      std::uint32_t a = j * (kSegments + 1) + i;
      std::uint32_t b = a + 1;
      std::uint32_t c = a + (kSegments + 1);
      std::uint32_t d = c + 1;
      md.addTriangle(a, c, b);
      md.addTriangle(b, c, d);
    }
  }
  mesh_.upload(md);
}

void Water::draw(const glm::mat4& viewProj, const glm::vec3& camPos,
                 const glm::vec3& sunDir, const glm::vec3& fogColor,
                 float fogDensity, float time) {
  shader_.use();
  shader_.set("uVP", viewProj);
  shader_.set("uTime", time);
  shader_.set("uCamPos", camPos);
  shader_.set("uSunDir", sunDir);
  shader_.set("uFogColor", fogColor);
  shader_.set("uFogDensity", fogDensity);

  gl.Enable(GL_BLEND);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.DepthMask(GL_FALSE);
  mesh_.draw();
  gl.DepthMask(GL_TRUE);
  gl.Disable(GL_BLEND);
}
