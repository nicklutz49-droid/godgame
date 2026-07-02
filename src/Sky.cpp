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
out vec4 FragColor;
void main() {
  vec3 d = normalize(vDir);
  vec3 zenith = vec3(0.28, 0.50, 0.76);
  vec3 col;
  if (d.y >= 0.0) {
    col = mix(uFogColor, zenith, smoothstep(0.0, 0.45, d.y));
  } else {
    // Below the horizon: match the fog exactly so far-clipped water blends in.
    col = uFogColor;
  }
  float s = max(dot(d, uSunDir), 0.0);
  col += vec3(1.0, 0.92, 0.72) * (pow(s, 400.0) * 1.2 + pow(s, 16.0) * 0.12);
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
               const glm::vec3& sunDir, const glm::vec3& fogColor) {
  shader_.use();
  shader_.set("uInvVP", invViewProj);
  shader_.set("uCamPos", camPos);
  shader_.set("uSunDir", sunDir);
  shader_.set("uFogColor", fogColor);

  gl.Disable(GL_DEPTH_TEST);
  gl.DepthMask(GL_FALSE);
  mesh_.draw();
  gl.DepthMask(GL_TRUE);
  gl.Enable(GL_DEPTH_TEST);
}
