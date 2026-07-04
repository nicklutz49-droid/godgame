#include "Sky.h"

namespace {

const char* kVertexSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uInvVP;
uniform vec3 uCamPos;
out vec3 vDir;
void main() {
  gl_Position = vec4(aPos.xy, 0.0, 1.0);
  vec4 p = uInvVP * vec4(aPos.xy, 1.0, 1.0);
  vDir = p.xyz / p.w - uCamPos;
}
)GLSL";

const char* kFragmentSrc = R"GLSL(
#version 330 core
in vec3 vDir;
uniform vec3 uSunDir;
uniform vec3 uFogColor;
uniform vec3 uZenith;
uniform vec3 uSunColor;
uniform float uNight;
out vec4 FragColor;
void main() {
  vec3 d = normalize(vDir);
  vec3 col;
  if (d.y >= 0.0) {
    col = mix(uFogColor, uZenith, smoothstep(0.0, 0.45, d.y));
  } else {
    // Below the horizon: match the fog exactly so far-clipped water blends in.
    col = uFogColor;
  }
  float s = max(dot(d, uSunDir), 0.0);
  col += uSunColor * (pow(s, 400.0) * 1.3 + pow(s, 16.0) * 0.14);
  // Stars after dark: sparse hash-lit cells, fading near the horizon.
  if (uNight > 0.01 && d.y > 0.0) {
    vec3 g = floor(d * 90.0);
    float h = fract(sin(dot(g, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
    float star = smoothstep(0.9962, 0.9990, h);
    col += vec3(0.85, 0.90, 1.0) * star * uNight * smoothstep(0.0, 0.15, d.y);
  }
  FragColor = vec4(col, 1.0);
}
)GLSL";

}  // namespace

void Sky::init() {
  shader_.compile(kVertexSrc, kFragmentSrc, "sky");

  // Single triangle covering the screen in NDC.
  MeshData md;
  md.addVertex(glm::vec3(-1, -1, 0), glm::vec3(0, 0, 1), glm::vec3(1));
  md.addVertex(glm::vec3(3, -1, 0), glm::vec3(0, 0, 1), glm::vec3(1));
  md.addVertex(glm::vec3(-1, 3, 0), glm::vec3(0, 0, 1), glm::vec3(1));
  md.addTriangle(0, 1, 2);
  mesh_.upload(md);
}

void Sky::draw(const glm::mat4& invViewProj, const glm::vec3& camPos,
               const glm::vec3& sunDir, const glm::vec3& fogColor,
               const glm::vec3& zenithColor, const glm::vec3& sunColor,
               float night) {
  shader_.use();
  shader_.set("uInvVP", invViewProj);
  shader_.set("uCamPos", camPos);
  shader_.set("uSunDir", sunDir);
  shader_.set("uFogColor", fogColor);
  shader_.set("uZenith", zenithColor);
  shader_.set("uSunColor", sunColor);
  shader_.set("uNight", night);

  gl.Disable(GL_DEPTH_TEST);
  gl.DepthMask(GL_FALSE);
  mesh_.draw();
  gl.DepthMask(GL_TRUE);
  gl.Enable(GL_DEPTH_TEST);
}
