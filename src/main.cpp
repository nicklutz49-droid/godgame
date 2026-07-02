// godgame - a Black & White inspired god game (first slice: land, camera, hand).
//
// Modes:
//   godgame                          interactive
//   godgame --seed 1234              interactive with a specific island seed
//   godgame --headless [steps]       no window; generate + simulate + self-test
//   godgame --screenshot out.bmp [frames] [far|close]   render offscreen-ish and dump a BMP

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Camera.h"
#include "Hand.h"
#include "Mesh.h"
#include "Shader.h"
#include "Sky.h"
#include "Terrain.h"
#include "Water.h"
#include "World.h"
#include "gl.h"

namespace {

// ---------------------------------------------------------------- lit shader

const char* kLitVertexSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uModel;
uniform mat4 uVP;
out vec3 vWorld;
out vec3 vNormal;
out vec3 vColor;
void main() {
  vec4 world = uModel * vec4(aPos, 1.0);
  vWorld = world.xyz;
  vNormal = mat3(uModel) * aNormal;
  vColor = aColor;
  gl_Position = uVP * world;
}
)GLSL";

const char* kLitFragmentSrc = R"GLSL(
#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;
uniform vec3 uSunDir;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uAlpha;
uniform float uEmissive;   // 0 = fully lit, 1 = raw albedo
out vec4 FragColor;
void main() {
  vec3 n = normalize(vNormal);
  float diff = max(dot(n, uSunDir), 0.0);
  float ambient = 0.40 * (0.9 + 0.25 * n.y);
  vec3 lit = vColor * (vec3(ambient) + diff * 0.95 * vec3(1.0, 0.96, 0.88));

  // Everything below the waterline picks up a submerged blue-green cast.
  if (vWorld.y < 0.0) {
    float k = clamp(-vWorld.y / 9.0, 0.0, 0.75);
    lit = mix(lit, vec3(0.10, 0.28, 0.38), k);
  }

  vec3 col = mix(lit, vColor, uEmissive);
  float dist = length(uCamPos - vWorld);
  float fog = 1.0 - exp(-uFogDensity * dist);
  col = mix(col, uFogColor, fog);
  FragColor = vec4(col, uAlpha);
}
)GLSL";

// ------------------------------------------------------- placeholder models

MeshData buildTreeMeshData(int variant) {
  static const glm::vec3 canopyColors[3] = {
      {0.20f, 0.42f, 0.16f}, {0.15f, 0.38f, 0.20f}, {0.25f, 0.46f, 0.15f}};
  static const float widths[3] = {1.0f, 0.88f, 1.12f};
  const glm::vec3 trunk(0.42f, 0.30f, 0.19f);
  const glm::vec3 leaf = canopyColors[variant];
  const float w = widths[variant];

  // Spans y in [-3.25, 3.25] so the prop origin is the model's center
  // (World::kTreeHalfHeight must match).
  MeshData md;
  md.addCylinder(glm::translate(glm::mat4(1.0f), {0, -1.95f, 0}), 0.30f, 0.24f, 2.6f, 7, trunk);
  md.addCylinder(glm::translate(glm::mat4(1.0f), {0, 0.40f, 0}), 1.90f * w, 0.0f, 2.8f, 8, leaf);
  md.addCylinder(glm::translate(glm::mat4(1.0f), {0, 1.50f, 0}), 1.50f * w, 0.0f, 2.4f, 8, leaf * 1.08f);
  md.addCylinder(glm::translate(glm::mat4(1.0f), {0, 2.40f, 0}), 1.05f * w, 0.0f, 1.7f, 8, leaf * 1.16f);
  return md;
}

MeshData buildRockMeshData(int variant) {
  MeshData md;
  md.addRock(glm::mat4(1.0f), 0.9f, 1337u * static_cast<std::uint32_t>(variant + 1),
             glm::vec3(0.48f, 0.46f, 0.44f));
  return md;
}

// Blocky placeholder for the divine hand: palm, four fingers, thumb.
// `curl` is the finger bend in radians (0 = open, ~1.3 = grabbing).
MeshData buildHandMeshData(float curl) {
  MeshData md;
  const glm::vec3 skin(0.93f, 0.90f, 0.84f);

  md.addBox(glm::mat4(1.0f), {0.55f, 0.12f, 0.50f}, skin);

  const float xs[4] = {-0.40f, -0.14f, 0.13f, 0.40f};
  const float halfLen[4] = {0.30f, 0.36f, 0.33f, 0.24f};
  for (int f = 0; f < 4; ++f) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, {xs[f], 0.02f, 0.50f});
    m = glm::rotate(m, 0.15f + curl, glm::vec3(1, 0, 0));
    m = glm::translate(m, {0.0f, 0.0f, halfLen[f]});
    md.addBox(m, {0.105f, 0.09f, halfLen[f]}, skin);
  }

  glm::mat4 t(1.0f);
  t = glm::translate(t, {-0.55f, 0.0f, 0.10f});
  t = glm::rotate(t, -0.75f + curl * 0.4f, glm::vec3(0, 1, 0));
  t = glm::rotate(t, 0.1f + curl * 0.6f, glm::vec3(1, 0, 0));
  t = glm::translate(t, {0.0f, 0.0f, 0.28f});
  md.addBox(t, {0.11f, 0.09f, 0.28f}, skin);
  return md;
}

MeshData buildShadowDiscData() {
  MeshData md;
  md.addDisc(glm::mat4(1.0f), 1.0f, 20, glm::vec3(0.02f, 0.04f, 0.02f));
  return md;
}

// Flat ocean floor continuing the heightfield's seabed out to the horizon,
// so the terrain mesh's square footprint is invisible through the water.
MeshData buildSeabedData() {
  MeshData md;
  const float e = 2600.0f;
  const float y = Terrain::SEABED;
  const glm::vec3 up(0, 1, 0);
  const glm::vec3 color(0.42f, 0.40f, 0.30f);
  md.addVertex({-e, y, -e}, up, color);
  md.addVertex({e, y, -e}, up, color);
  md.addVertex({e, y, e}, up, color);
  md.addVertex({-e, y, e}, up, color);
  md.addTriangle(0, 2, 1);
  md.addTriangle(0, 3, 2);
  return md;
}

// --------------------------------------------------------------- BMP output

bool writeBMP(const char* path, int w, int h, const std::vector<unsigned char>& rgbBottomUp) {
  const int rowSize = (3 * w + 3) & ~3;
  const int dataSize = rowSize * h;
  const int fileSize = 54 + dataSize;

  unsigned char header[54] = {};
  header[0] = 'B';
  header[1] = 'M';
  auto put32 = [&](int off, unsigned v) {
    header[off] = v & 0xFF;
    header[off + 1] = (v >> 8) & 0xFF;
    header[off + 2] = (v >> 16) & 0xFF;
    header[off + 3] = (v >> 24) & 0xFF;
  };
  put32(2, fileSize);
  put32(10, 54);
  put32(14, 40);
  put32(18, static_cast<unsigned>(w));
  put32(22, static_cast<unsigned>(h));
  header[26] = 1;
  header[28] = 24;

  FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  std::fwrite(header, 1, 54, fp);
  std::vector<unsigned char> row(rowSize, 0);
  for (int y = 0; y < h; ++y) {
    const unsigned char* src = &rgbBottomUp[static_cast<size_t>(y) * w * 3];
    for (int x = 0; x < w; ++x) {
      row[x * 3 + 0] = src[x * 3 + 2];
      row[x * 3 + 1] = src[x * 3 + 1];
      row[x * 3 + 2] = src[x * 3 + 0];
    }
    std::fwrite(row.data(), 1, rowSize, fp);
  }
  std::fclose(fp);
  return true;
}

glm::mat4 orientToNormal(const glm::vec3& n) {
  if (n.y > 0.999f) return glm::mat4(1.0f);
  glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0, 1, 0), n));
  return glm::rotate(glm::mat4(1.0f), std::acos(std::clamp(n.y, -1.0f, 1.0f)), axis);
}

// -------------------------------------------------------------------- app

struct App {
  SDL_Window* window = nullptr;
  SDL_GLContext ctx = nullptr;
  int winW = 1280, winH = 720;

  Camera cam;
  World world;
  Hand hand;
  Sky sky;
  Water water;
  Shader lit;
  Mesh terrainMesh;
  Mesh treeMeshes[3];
  Mesh rockMeshes[3];
  Mesh handOpen;
  Mesh handClosed;
  Mesh shadowDisc;
  Mesh seabed;

  std::uint32_t seed = 20260702u;
  bool quit = false;
  bool wireframe = false;
  bool panning = false;
  bool orbiting = false;
  glm::vec3 grabPoint{0.0f};
  float wheelAccum = 0.0f;
  int mouseX = 0, mouseY = 0;
  float groundLift = 0.0f;

  glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.62f, 0.30f));
  glm::vec3 fogColor{0.74f, 0.82f, 0.88f};
  float fogDensity = 0.0009f;

  bool initGraphics();
  void initScene();
  void rebuildWorld(std::uint32_t newSeed);
  void handleEvent(const SDL_Event& e);
  void update(float dt);
  void render(float time);
  int runInteractive();
  int runScreenshot(const std::string& path, int frames, const std::string& view);
  void shutdown();
};

bool App::initGraphics() {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  // Try with 4x MSAA first, fall back to no MSAA.
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
  window = SDL_CreateWindow("godgame", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            winW, winH, flags);
  if (window) ctx = SDL_GL_CreateContext(window);
  if (!ctx) {
    if (window) SDL_DestroyWindow(window);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    window = SDL_CreateWindow("godgame", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              winW, winH, flags);
    if (!window) {
      SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
      return false;
    }
    ctx = SDL_GL_CreateContext(window);
  }
  if (!ctx) {
    SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
    return false;
  }

  SDL_GL_SetSwapInterval(1);
  if (!gl.loadAll()) return false;

  SDL_Log("OpenGL: %s / %s", reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)),
          reinterpret_cast<const char*>(gl.GetString(GL_VERSION)));

  gl.Enable(GL_DEPTH_TEST);
  gl.DepthFunc(GL_LEQUAL);
  gl.Enable(GL_CULL_FACE);
  gl.CullFace(GL_BACK);
  gl.FrontFace(GL_CCW);
  gl.Enable(GL_MULTISAMPLE);

  SDL_ShowCursor(SDL_DISABLE);
  return true;
}

void App::initScene() {
  lit.compile(kLitVertexSrc, kLitFragmentSrc, "lit");
  sky.init();
  water.init();

  for (int v = 0; v < 3; ++v) {
    treeMeshes[v].upload(buildTreeMeshData(v));
    rockMeshes[v].upload(buildRockMeshData(v));
  }
  handOpen.upload(buildHandMeshData(0.0f));
  handClosed.upload(buildHandMeshData(1.25f));
  shadowDisc.upload(buildShadowDiscData());
  seabed.upload(buildSeabedData());

  rebuildWorld(seed);
}

void App::rebuildWorld(std::uint32_t newSeed) {
  seed = newSeed;
  world.generate(seed);
  terrainMesh.upload(world.terrain.buildMeshData());
  hand.mode = Hand::Mode::Free;
  hand.heldProp = -1;
  SDL_Log("World seed %u | land %.0f%% | height %.1f..%.1f | %zu props", seed,
          world.terrain.landFraction() * 100.0f, world.terrain.minHeight(),
          world.terrain.maxHeight(), world.props.size());
}

void App::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_QUIT:
      quit = true;
      break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        winW = e.window.data1;
        winH = e.window.data2;
      }
      break;
    case SDL_MOUSEMOTION:
      mouseX = e.motion.x;
      mouseY = e.motion.y;
      if (orbiting) cam.orbit(static_cast<float>(e.motion.xrel), static_cast<float>(e.motion.yrel));
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == SDL_BUTTON_LEFT) {
        if (!hand.tryGrab(world) && hand.hasGround) {
          panning = true;
          grabPoint = hand.groundPoint;
        }
      } else if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) {
        orbiting = true;
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT) {
        hand.release(world);
        panning = false;
      } else if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) {
        orbiting = false;
      }
      break;
    case SDL_MOUSEWHEEL:
      wheelAccum += static_cast<float>(e.wheel.y);
      break;
    case SDL_KEYDOWN:
      if (e.key.repeat) break;
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE:
          quit = true;
          break;
        case SDLK_F2:
          wireframe = !wireframe;
          break;
        case SDLK_r:
          rebuildWorld(seed * 1664525u + 1013904223u);
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void App::update(float dt) {
  const Uint8* keys = SDL_GetKeyboardState(nullptr);

  glm::vec3 forward(std::sin(cam.yaw), 0.0f, std::cos(cam.yaw));
  glm::vec3 right(-forward.z, 0.0f, forward.x);
  glm::vec3 move(0.0f);
  if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) move += forward;
  if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) move -= forward;
  if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move += right;
  if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) move -= right;
  if (glm::length(move) > 0.01f) {
    float speed = cam.distance * 0.8f;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) speed *= 2.5f;
    cam.focus += glm::normalize(move) * speed * dt;
  }
  if (keys[SDL_SCANCODE_Q]) cam.yaw += 1.6f * dt;
  if (keys[SDL_SCANCODE_E]) cam.yaw -= 1.6f * dt;

  // Grab-the-land panning: keep the grabbed point under the cursor.
  if (panning && !orbiting) {
    glm::vec3 dir = cam.rayDir(static_cast<float>(mouseX), static_cast<float>(mouseY), winW, winH);
    glm::vec3 pos = cam.position();
    if (dir.y < -0.05f) {
      float t = (grabPoint.y - pos.y) / dir.y;
      if (t > 0.0f && t < 3000.0f) {
        glm::vec3 hit = pos + dir * t;
        glm::vec3 delta = grabPoint - hit;
        delta.y = 0.0f;
        float len = glm::length(delta);
        if (len > 80.0f) delta *= 80.0f / len;
        cam.focus += delta;
      }
    }
  }

  if (wheelAccum != 0.0f) {
    if (hand.hasGround)
      cam.zoomBy(wheelAccum, &hand.groundPoint);
    else
      cam.zoomBy(wheelAccum, nullptr);
    wheelAccum = 0.0f;
  }

  const float bound = Terrain::SIZE * 0.55f;
  cam.focus.x = std::clamp(cam.focus.x, -bound, bound);
  cam.focus.z = std::clamp(cam.focus.z, -bound, bound);
  float focusTargetY = std::max(world.terrain.heightAt(cam.focus.x, cam.focus.z),
                                Terrain::WATER_LEVEL);
  cam.focus.y += (focusTargetY - cam.focus.y) * std::min(1.0f, 8.0f * dt);

  // Keep the eye out of hillsides.
  cam.groundLift = 0.0f;
  glm::vec3 eye = cam.position();
  float minEyeY = world.terrain.heightAt(eye.x, eye.z) + 2.5f;
  float liftTarget = std::max(0.0f, minEyeY - eye.y);
  groundLift += (liftTarget - groundLift) * std::min(1.0f, 10.0f * dt);
  cam.groundLift = groundLift;

  glm::vec3 rayOrigin = cam.position();
  glm::vec3 rayDir = cam.rayDir(static_cast<float>(mouseX), static_cast<float>(mouseY), winW, winH);
  hand.update(dt, rayOrigin, rayDir, world);

  world.update(dt);
}

void App::render(float time) {
  int dw = winW, dh = winH;
  SDL_GL_GetDrawableSize(window, &dw, &dh);
  gl.Viewport(0, 0, dw, dh);
  gl.ClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  float aspect = dh > 0 ? static_cast<float>(dw) / static_cast<float>(dh) : 1.0f;
  glm::mat4 view = cam.view();
  glm::mat4 proj = cam.proj(aspect);
  glm::mat4 vp = proj * view;
  glm::vec3 camPos = cam.position();

  sky.draw(glm::inverse(vp), camPos, sunDir, fogColor);

  lit.use();
  lit.set("uVP", vp);
  lit.set("uSunDir", sunDir);
  lit.set("uCamPos", camPos);
  lit.set("uFogColor", fogColor);
  lit.set("uFogDensity", fogDensity);
  lit.set("uAlpha", 1.0f);
  lit.set("uEmissive", 0.0f);

  if (wireframe) gl.PolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  lit.set("uModel", glm::mat4(1.0f));
  terrainMesh.draw();
  seabed.draw();

  for (std::size_t i = 0; i < world.props.size(); ++i) {
    const Prop& p = world.props[i];
    glm::mat4 model = glm::translate(glm::mat4(1.0f), p.pos) * glm::mat4_cast(p.rot) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(p.scale));
    lit.set("uModel", model);
    lit.set("uEmissive", static_cast<int>(i) == hand.hoverProp ? 0.22f : 0.0f);
    const Mesh& mesh = p.type == PropType::Tree ? treeMeshes[p.variant] : rockMeshes[p.variant];
    mesh.draw();
  }
  lit.set("uEmissive", 0.0f);

  if (wireframe) gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // Blob shadows under airborne/carried props (cheap read of where things land).
  gl.Enable(GL_BLEND);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.DepthMask(GL_FALSE);
  lit.set("uEmissive", 1.0f);
  lit.set("uAlpha", 0.35f);
  for (const Prop& p : world.props) {
    if (!p.held && p.asleep) continue;
    float ground = world.terrain.heightAt(p.pos.x, p.pos.z);
    if (ground < Terrain::WATER_LEVEL - 0.2f) continue;
    if (p.pos.y - ground < 0.2f) continue;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(p.pos.x, ground + 0.08f, p.pos.z)) *
                      orientToNormal(world.terrain.normalAt(p.pos.x, p.pos.z)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(p.radius * 1.15f));
    lit.set("uModel", model);
    shadowDisc.draw();
  }
  gl.DepthMask(GL_TRUE);
  gl.Disable(GL_BLEND);

  water.draw(vp, camPos, sunDir, fogColor, fogDensity, time);

  // The divine hand, drawn last.
  gl.Enable(GL_BLEND);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.DepthMask(GL_FALSE);
  lit.use();
  float handScale = std::clamp(cam.distance * 0.045f, 1.2f, 8.0f);
  glm::mat4 handModel = glm::translate(glm::mat4(1.0f), hand.pos) *
                        glm::rotate(glm::mat4(1.0f), cam.yaw, glm::vec3(0, 1, 0)) *
                        glm::rotate(glm::mat4(1.0f), -0.30f, glm::vec3(1, 0, 0)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(handScale));
  lit.set("uModel", handModel);
  lit.set("uAlpha", 0.95f);
  lit.set("uEmissive", 0.35f);
  const bool closed = hand.mode == Hand::Mode::Carry || panning;
  (closed ? handClosed : handOpen).draw();
  lit.set("uAlpha", 1.0f);
  lit.set("uEmissive", 0.0f);
  gl.DepthMask(GL_TRUE);
  gl.Disable(GL_BLEND);

  SDL_GL_SwapWindow(window);
}

int App::runInteractive() {
  SDL_Log("Controls:");
  SDL_Log("  Left-drag ground   pan (grab the land)");
  SDL_Log("  Left-drag object   pick up; release while moving to throw");
  SDL_Log("  Right/middle-drag  rotate & tilt camera");
  SDL_Log("  Mouse wheel        zoom toward cursor");
  SDL_Log("  WASD/arrows Q E    move / rotate, Shift = faster");
  SDL_Log("  R                  new island   F2 wireframe   Esc quit");

  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  float time = 0.0f;
  float fpsTimer = 0.0f;
  int fpsFrames = 0;

  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e);

    Uint64 now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>(static_cast<double>(now - prev) / freq);
    prev = now;
    dt = std::min(dt, 0.05f);
    time += dt;

    update(dt);
    render(time);

    fpsTimer += dt;
    ++fpsFrames;
    if (fpsTimer >= 0.5f) {
      char title[64];
      std::snprintf(title, sizeof(title), "godgame - %.0f fps",
                    fpsFrames / fpsTimer);
      SDL_SetWindowTitle(window, title);
      fpsTimer = 0.0f;
      fpsFrames = 0;
    }
  }
  return 0;
}

int App::runScreenshot(const std::string& path, int frames, const std::string& view) {
  if (view == "close") {
    // Frame the first tree from up close.
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    for (const Prop& p : world.props) {
      if (p.type == PropType::Tree) {
        target = p.pos;
        break;
      }
    }
    cam.focus = target;
    cam.distance = 45.0f;
    cam.yaw = 2.2f;
  } else {
    cam.focus = glm::vec3(0.0f, 8.0f, 0.0f);
    cam.distance = 300.0f;
    cam.yaw = 0.6f;
  }
  mouseX = winW / 2;
  mouseY = winH / 2;

  float time = 0.0f;
  for (int i = 0; i < frames; ++i) {
    update(1.0f / 60.0f);
    render(time);
    time += 1.0f / 60.0f;
  }

  int dw = winW, dh = winH;
  SDL_GL_GetDrawableSize(window, &dw, &dh);
  std::vector<unsigned char> pixels(static_cast<size_t>(dw) * dh * 3);
  gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
  gl.ReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  if (!writeBMP(path.c_str(), dw, dh, pixels)) {
    SDL_Log("Failed to write %s", path.c_str());
    return 1;
  }
  SDL_Log("Wrote %s (%dx%d)", path.c_str(), dw, dh);
  return 0;
}

void App::shutdown() {
  if (ctx) SDL_GL_DeleteContext(ctx);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
}

// ------------------------------------------------------------ headless test

int runHeadless(std::uint32_t seed, int steps) {
  World world;
  world.generate(seed);

  std::printf("seed          %u\n", seed);
  std::printf("land fraction %.3f\n", world.terrain.landFraction());
  std::printf("height range  %.2f .. %.2f\n", world.terrain.minHeight(),
              world.terrain.maxHeight());
  std::size_t trees = 0, rocks = 0;
  for (const Prop& p : world.props)
    (p.type == PropType::Tree ? trees : rocks) += 1;
  std::printf("props         %zu trees, %zu rocks\n", trees, rocks);

  // Self-test: hurl the first rock across the island and make sure it flies,
  // lands, and goes to sleep with sane numbers.
  int rockIndex = -1;
  for (std::size_t i = 0; i < world.props.size(); ++i) {
    if (world.props[i].type == PropType::Rock) {
      rockIndex = static_cast<int>(i);
      break;
    }
  }
  if (rockIndex < 0) {
    std::printf("FAIL: no rock spawned\n");
    return 1;
  }
  Prop& rock = world.props[rockIndex];
  rock.pos = glm::vec3(0.0f, world.terrain.heightAt(0.0f, 0.0f) + 30.0f, 0.0f);
  world.throwProp(rockIndex, glm::vec3(18.0f, 6.0f, 11.0f));
  glm::vec3 start = rock.pos;

  const float dt = 1.0f / 60.0f;
  for (int i = 0; i < steps; ++i) world.update(dt);

  bool finite = std::isfinite(rock.pos.x) && std::isfinite(rock.pos.y) &&
                std::isfinite(rock.pos.z);
  float travelled = glm::distance(glm::vec2(start.x, start.z),
                                  glm::vec2(rock.pos.x, rock.pos.z));
  std::printf("thrown rock   travelled %.1f, final (%.1f, %.1f, %.1f), asleep=%d\n",
              travelled, rock.pos.x, rock.pos.y, rock.pos.z, rock.asleep ? 1 : 0);

  bool ok = finite && travelled > 5.0f && rock.asleep &&
            world.terrain.landFraction() > 0.15f && world.terrain.landFraction() < 0.8f &&
            trees > 50 && rocks > 30;
  std::printf(ok ? "OK\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::uint32_t seed = 20260702u;
  bool headless = false;
  int headlessSteps = 900;
  std::string screenshotPath;
  int screenshotFrames = 90;
  std::string screenshotView = "far";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) {
      seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (arg == "--headless") {
      headless = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') headlessSteps = std::atoi(argv[++i]);
    } else if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
      if (i + 1 < argc && argv[i + 1][0] != '-') screenshotFrames = std::atoi(argv[++i]);
      if (i + 1 < argc && argv[i + 1][0] != '-') screenshotView = argv[++i];
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
      return 2;
    }
  }

  if (headless) return runHeadless(seed, headlessSteps);

  App app;
  app.seed = seed;
  if (!app.initGraphics()) return 1;
  app.initScene();

  int rc;
  if (!screenshotPath.empty())
    rc = app.runScreenshot(screenshotPath, screenshotFrames, screenshotView);
  else
    rc = app.runInteractive();

  app.shutdown();
  return rc;
}
