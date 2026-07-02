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
#include "Models.h"
#include "Shader.h"
#include "Sky.h"
#include "Terrain.h"
#include "Tuning.h"
#include "Villagers.h"
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
uniform vec3 uSunColor;    // day/night sunlight tint (from DayCycle)
uniform vec3 uAmbient;     // ambient light color (from DayCycle)
uniform vec3 uTint;        // per-draw albedo multiplier (job colors, flashes)
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uAlpha;
uniform float uEmissive;   // 0 = fully lit, 1 = raw albedo
out vec4 FragColor;
void main() {
  vec3 n = normalize(vNormal);
  float diff = max(dot(n, uSunDir), 0.0);
  vec3 albedo = vColor * uTint;
  vec3 lit = albedo * (uAmbient * (0.9 + 0.25 * n.y) + diff * uSunColor);

  // Everything below the waterline picks up a submerged blue-green cast.
  if (vWorld.y < 0.0) {
    float k = clamp(-vWorld.y / 9.0, 0.0, 0.75);
    lit = mix(lit, vec3(0.10, 0.28, 0.38), k);
  }

  vec3 col = mix(lit, albedo, uEmissive);
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

// A terrain-following band at `radius` around `center` - the influence ring.
MeshData buildRingMeshData(const Terrain& terrain, glm::vec2 center, float radius) {
  MeshData md;
  const int kSegments = 120;
  const float kHalfWidth = 0.55f;
  const glm::vec3 gold(1.0f, 0.88f, 0.45f);
  for (int s = 0; s <= kSegments; ++s) {
    float a = 6.2831853f * static_cast<float>(s) / kSegments;
    glm::vec2 dir(std::sin(a), std::cos(a));
    for (float r : {radius - kHalfWidth, radius + kHalfWidth}) {
      glm::vec2 p = center + dir * r;
      float y = std::max(terrain.heightAt(p.x, p.y), Terrain::WATER_LEVEL) + 0.18f;
      md.addVertex(glm::vec3(p.x, y, p.y), glm::vec3(0, 1, 0), gold);
    }
  }
  for (int s = 0; s < kSegments; ++s) {
    std::uint32_t a = static_cast<std::uint32_t>(s) * 2;
    md.addTriangle(a, a + 1, a + 2);
    md.addTriangle(a + 1, a + 3, a + 2);
  }
  return md;
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

  // Village-slice meshes.
  Mesh logMesh, foodMesh, stumpMesh;
  Mesh villagerHeads[3], villagerTorso, villagerArm, villagerLeg;
  Mesh houseStages[4], houseWindows, totemMesh, storagePadMesh;
  Mesh woodPileMesh, foodPileMesh, campfireMesh, flameMesh;
  Mesh fieldSlabMesh, cropMesh, smokeDisc;
  Mesh bubbleHungerMesh, bubbleSleepMesh, bubbleFearMesh;
  Mesh templeMesh, templeCrystalMesh;
  Mesh templeRing, villageRing;
  float lastVillageRingR = -1.0f;

  // Short-lived cast feedback (expanding gold pulse at miracle points).
  struct CastEffect {
    glm::vec3 pos;
    float age;
  };
  std::vector<CastEffect> effects;

  std::uint32_t seed = 20260702u;
  bool quit = false;
  bool wireframe = false;
  bool panning = false;
  bool orbiting = false;
  glm::vec3 grabPoint{0.0f};
  float wheelAccum = 0.0f;
  int mouseX = 0, mouseY = 0;
  float groundLift = 0.0f;

  // Debug/tuning toggles.
  int simSpeed = 1;        // F4: 1x / 4x / 16x
  bool stateTint = false;  // F3: tint villagers by state instead of job
  glm::vec3 prevHandPos{0.0f};

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

  logMesh.upload(models::logProp());
  foodMesh.upload(models::foodBundleProp());
  stumpMesh.upload(models::stumpProp());
  for (int v = 0; v < 3; ++v) villagerHeads[v].upload(models::villagerHead(v));
  villagerTorso.upload(models::villagerTorso());
  villagerArm.upload(models::villagerArm());
  villagerLeg.upload(models::villagerLeg());
  for (int s = 0; s < 4; ++s) houseStages[s].upload(models::houseStage(s));
  houseWindows.upload(models::houseWindows());
  totemMesh.upload(models::totem());
  storagePadMesh.upload(models::storagePad());
  woodPileMesh.upload(models::woodPile());
  foodPileMesh.upload(models::foodPile());
  campfireMesh.upload(models::campfire());
  flameMesh.upload(models::campfireFlame());
  fieldSlabMesh.upload(models::fieldSlab(8.0f, 5.0f));
  cropMesh.upload(models::cropCone());
  {
    MeshData md;
    md.addDisc(glm::mat4(1.0f), 1.0f, 16, glm::vec3(0.80f, 0.80f, 0.80f));
    smokeDisc.upload(md);
  }
  bubbleHungerMesh.upload(models::bubbleHunger());
  bubbleSleepMesh.upload(models::bubbleSleep());
  bubbleFearMesh.upload(models::bubbleFear());
  templeMesh.upload(models::temple());
  templeCrystalMesh.upload(models::templeCrystal());

  rebuildWorld(seed);
}

void App::rebuildWorld(std::uint32_t newSeed) {
  seed = newSeed;
  world.generate(seed);
  terrainMesh.upload(world.terrain.buildMeshData());
  hand.mode = Hand::Mode::Free;
  hand.held.clear();
  hand.hover.clear();
  effects.clear();
  if (world.temple.founded)
    templeRing.upload(buildRingMeshData(
        world.terrain, glm::vec2(world.temple.pos.x, world.temple.pos.z),
        tune::kTempleInfluence));
  if (world.village.founded) {
    lastVillageRingR = world.village.influenceRadius();
    villageRing.upload(buildRingMeshData(
        world.terrain, glm::vec2(world.village.center.x, world.village.center.z),
        lastVillageRingR));
  }
  SDL_Log("World seed %u | land %.0f%% | height %.1f..%.1f | %zu props | village (%.0f, %.0f), pop %d",
          seed, world.terrain.landFraction() * 100.0f, world.terrain.minHeight(),
          world.terrain.maxHeight(), world.props.size(), world.village.center.x,
          world.village.center.z, world.village.population());
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
        if (hand.mode == Hand::Mode::Carry) {
          hand.release(world);
          SDL_Log("release speed %.1f (%s)", hand.lastReleaseSpeed,
                  hand.lastReleaseSpeed < tune::kPlaceSpeed ? "place" : "throw");
        }
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
        case SDLK_F3:
          stateTint = !stateTint;
          break;
        case SDLK_F4:
          simSpeed = simSpeed == 1 ? 4 : (simSpeed == 4 ? 16 : 1);
          SDL_Log("sim speed x%d", simSpeed);
          break;
        case SDLK_t:
          world.dayCycle.t += 0.02f;
          world.dayCycle.t -= std::floor(world.dayCycle.t);
          break;
        case SDLK_k:
          if (hand.hasGround && world.village.founded) {
            Villager v;
            v.pos = hand.groundPoint;
            v.pos.y = world.terrain.heightAt(v.pos.x, v.pos.z);
            v.rng = seed ^ (static_cast<std::uint32_t>(world.village.villagers.size()) *
                            2654435761u);
            v.variant = static_cast<int>(world.village.villagers.size() % 3);
            world.village.villagers.push_back(v);
          }
          break;
        case SDLK_l:
          world.village.wood += 10;
          world.village.food += 10;
          break;
        case SDLK_m:
          if (hand.hasGround) {
            if (world.castFoodMiracle(hand.groundPoint)) {
              effects.push_back({hand.groundPoint, 0.0f});
              SDL_Log("food miracle! mana %.0f/%.0f", world.temple.mana,
                      world.temple.manaMax);
            } else if (!world.insideInfluence(hand.groundPoint)) {
              SDL_Log("cannot cast: outside your influence");
            } else {
              SDL_Log("cannot cast: need %.0f mana (have %.0f)",
                      tune::kFoodMiracleCost, world.temple.mana);
            }
          }
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

  // Tell the sim where the hand is (villagers watch it).
  world.handPos = hand.pos;
  world.handSpeed = dt > 0.0001f ? glm::distance(hand.pos, prevHandPos) / dt : 0.0f;
  prevHandPos = hand.pos;

  // F4 time-lapse scales the sim only; camera and hand stay real-time.
  for (int step = 0; step < simSpeed; ++step) world.update(dt);

  // The village ring grows/shrinks with belief; rebuild it on real change.
  if (world.village.founded &&
      std::abs(world.village.influenceRadius() - lastVillageRingR) > 0.75f) {
    lastVillageRingR = world.village.influenceRadius();
    villageRing.upload(buildRingMeshData(
        world.terrain, glm::vec2(world.village.center.x, world.village.center.z),
        lastVillageRingR));
  }

  for (CastEffect& e : effects) e.age += dt;
  effects.erase(std::remove_if(effects.begin(), effects.end(),
                               [](const CastEffect& e) { return e.age > 1.2f; }),
                effects.end());
}

namespace {

glm::vec3 jobTint(Job j) {
  switch (j) {
    case Job::Forester: return {0.45f, 0.80f, 0.38f};
    case Job::Farmer: return {0.95f, 0.80f, 0.42f};
    case Job::Fisherman: return {0.45f, 0.65f, 0.95f};
    case Job::Builder: return {0.90f, 0.50f, 0.35f};
    case Job::Worshipper: return {1.0f, 0.94f, 0.55f};  // robed in gold
    default: return {0.85f, 0.82f, 0.75f};  // undyed
  }
}

glm::vec3 stateTintColor(VState s) {
  if (s == VState::Held || s == VState::Airborne || s == VState::Stunned ||
      s == VState::Swim)
    return {1.0f, 0.25f, 0.25f};
  if (s == VState::Panic || s == VState::Cower) return {1.0f, 0.6f, 0.2f};
  if (s == VState::Work || s == VState::Haul) return {0.3f, 1.0f, 0.4f};
  if (s == VState::GoTo || s == VState::Wander || s == VState::GoEat ||
      s == VState::GoHome)
    return {0.35f, 0.55f, 1.0f};
  if (s == VState::Sleep || s == VState::Eat) return {0.8f, 0.5f, 1.0f};
  return {0.7f, 0.7f, 0.7f};
}

}  // namespace

void App::render(float time) {
  int dw = winW, dh = winH;
  SDL_GL_GetDrawableSize(window, &dw, &dh);
  gl.Viewport(0, 0, dw, dh);

  // Lighting follows the day cycle; noon matches the original fixed look.
  const DayCycle& day = world.dayCycle;
  sunDir = day.sunDir();
  fogColor = day.fogColor();
  glm::vec3 sunColor = day.sunColor();
  glm::vec3 ambient = day.ambient();
  float night = 1.0f - day.daylight();

  gl.ClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  float aspect = dh > 0 ? static_cast<float>(dw) / static_cast<float>(dh) : 1.0f;
  glm::mat4 view = cam.view();
  glm::mat4 proj = cam.proj(aspect);
  glm::mat4 vp = proj * view;
  glm::vec3 camPos = cam.position();

  sky.draw(glm::inverse(vp), camPos, sunDir, fogColor, day.zenithColor(), sunColor,
           night);

  lit.use();
  lit.set("uVP", vp);
  lit.set("uSunDir", sunDir);
  lit.set("uSunColor", sunColor);
  lit.set("uAmbient", ambient);
  lit.set("uTint", glm::vec3(1.0f));
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
    if (!p.alive) continue;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), p.pos) * glm::mat4_cast(p.rot) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(p.scale));
    lit.set("uModel", model);
    lit.set("uEmissive",
            hand.hover.isProp() && hand.hover.index == static_cast<int>(i) ? 0.22f
                                                                           : 0.0f);
    const Mesh* mesh = nullptr;
    switch (p.type) {
      case PropType::Tree: mesh = &treeMeshes[p.variant]; break;
      case PropType::Rock: mesh = &rockMeshes[p.variant]; break;
      case PropType::Log: mesh = &logMesh; break;
      case PropType::Food: mesh = &foodMesh; break;
      case PropType::Stump: mesh = &stumpMesh; break;
    }
    if (mesh) mesh->draw();
  }
  lit.set("uEmissive", 0.0f);

  // Village buildings. The totem glows when worshippers are dancing.
  const Village& vil = world.village;
  int dancers = vil.activeWorshippers();
  float totemGlow =
      dancers > 0 ? std::min(0.5f, 0.15f * static_cast<float>(dancers)) +
                        0.05f * std::sin(time * 2.3f)
                  : 0.0f;
  for (std::size_t b = 0; b < vil.buildings.size(); ++b) {
    const Building& bd = vil.buildings[b];
    if (bd.stage < 0) continue;  // reserved plot, invisible
    glm::mat4 model = glm::translate(glm::mat4(1.0f), bd.pos) *
                      glm::rotate(glm::mat4(1.0f), bd.yaw, glm::vec3(0, 1, 0));
    lit.set("uModel", model);
    switch (bd.type) {
      case BuildingType::Center:
        lit.set("uEmissive", totemGlow);
        totemMesh.draw();
        lit.set("uEmissive", 0.0f);
        break;
      case BuildingType::Storage: storagePadMesh.draw(); break;
      case BuildingType::Campfire: campfireMesh.draw(); break;
      case BuildingType::House: houseStages[std::clamp(bd.stage, 0, 3)].draw(); break;
    }
  }

  // The temple: the god's seat, its crystal glowing with stored mana.
  if (world.temple.founded) {
    glm::mat4 tm = glm::translate(glm::mat4(1.0f), world.temple.pos) *
                   glm::rotate(glm::mat4(1.0f), world.temple.yaw, glm::vec3(0, 1, 0));
    lit.set("uModel", tm);
    templeMesh.draw();
    float manaFrac = world.temple.manaMax > 0.0f
                         ? world.temple.mana / world.temple.manaMax
                         : 0.0f;
    // The beacon floats above the roof so the mana level reads from anywhere.
    lit.set("uEmissive",
            0.30f + 0.60f * manaFrac + 0.05f * std::sin(time * 3.1f));
    lit.set("uModel", tm * glm::translate(glm::mat4(1.0f),
                                          glm::vec3(0, 4.9f + 0.25f * std::sin(time * 1.1f), 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(1.25f)));
    templeCrystalMesh.draw();
    lit.set("uEmissive", 0.0f);
  }
  if (vil.founded) {
    // Stock piles scale with the stores - a glanceable economy gauge.
    glm::vec3 sp = vil.storagePos();
    if (vil.wood > 0) {
      float s = std::clamp(0.45f + static_cast<float>(vil.wood) * 0.025f, 0.45f, 1.5f);
      lit.set("uModel", glm::translate(glm::mat4(1.0f), sp + glm::vec3(1.7f, 0.15f, 0.6f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(s)));
      woodPileMesh.draw();
    }
    if (vil.food > 0) {
      float s = std::clamp(0.45f + static_cast<float>(vil.food) * 0.02f, 0.45f, 1.4f);
      lit.set("uModel", glm::translate(glm::mat4(1.0f), sp + glm::vec3(-1.6f, 0.15f, -0.7f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(s)));
      foodPileMesh.draw();
    }
    // The field and its crops.
    float fh = world.terrain.heightAt(vil.fieldCenter.x, vil.fieldCenter.y);
    lit.set("uModel", glm::translate(glm::mat4(1.0f),
                                     glm::vec3(vil.fieldCenter.x, fh, vil.fieldCenter.y)));
    fieldSlabMesh.draw();
    for (const FarmCell& c : vil.farmCells) {
      if (c.growth < 0.08f) continue;
      float ch = world.terrain.heightAt(c.pos.x, c.pos.y);
      glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(c.pos.x, ch + 0.15f, c.pos.y)) *
                    glm::scale(glm::mat4(1.0f),
                               glm::vec3(0.55f + 0.45f * c.growth, c.growth, 0.55f + 0.45f * c.growth));
      lit.set("uModel", m);
      cropMesh.draw();
    }
  }

  // Villagers: six posed parts each, two at distance.
  for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
    const Villager& v = vil.villagers[i];
    if (v.inside) continue;
    VillagerPose pose = computeVillagerPose(v, time);
    glm::mat4 root = glm::translate(glm::mat4(1.0f), v.pos) * pose.root;
    bool farAway = glm::distance(camPos, v.pos) > 180.0f;
    bool hovered = hand.hover.isVillager() && hand.hover.index == static_cast<int>(i);
    glm::vec3 tint = stateTint ? stateTintColor(v.state) : jobTint(v.job);
    if (v.assignedFlash > 0.0f)
      tint = glm::mix(tint, glm::vec3(1.4f), 0.5f * std::sin(v.assignedFlash * 9.0f) + 0.5f);
    float emissive = hovered ? 0.25f : (v.assignedFlash > 0.0f ? 0.2f : 0.0f);
    lit.set("uEmissive", emissive);

    lit.set("uTint", tint);
    lit.set("uModel", root * pose.torso);
    villagerTorso.draw();
    if (!farAway) {
      lit.set("uModel", root * pose.armL);
      villagerArm.draw();
      lit.set("uModel", root * pose.armR);
      villagerArm.draw();
      lit.set("uTint", glm::vec3(1.0f));
      lit.set("uModel", root * pose.legL);
      villagerLeg.draw();
      lit.set("uModel", root * pose.legR);
      villagerLeg.draw();
    } else {
      lit.set("uTint", glm::vec3(1.0f));
    }
    lit.set("uModel", root * pose.head);
    villagerHeads[v.variant % 3].draw();
    lit.set("uTint", glm::vec3(1.0f));
  }
  lit.set("uEmissive", 0.0f);

  if (wireframe) gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // ---- translucent pass: shadows, bubbles, fire, smoke, window glow ----
  gl.Enable(GL_BLEND);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.DepthMask(GL_FALSE);
  lit.set("uEmissive", 1.0f);
  lit.set("uAlpha", 0.35f);
  for (const Prop& p : world.props) {
    if (!p.alive) continue;
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
  for (const Villager& v : vil.villagers) {
    if (!(v.held || v.state == VState::Airborne)) continue;
    float ground = world.terrain.heightAt(v.pos.x, v.pos.z);
    if (ground < Terrain::WATER_LEVEL - 0.2f) continue;
    if (v.pos.y - ground < 0.2f) continue;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(v.pos.x, ground + 0.08f, v.pos.z)) *
                      orientToNormal(world.terrain.normalAt(v.pos.x, v.pos.z)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(0.9f * v.scale));
    lit.set("uModel", model);
    shadowDisc.draw();
  }

  // Thought bubbles: needs and fear, yaw-billboarded.
  for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
    const Villager& v = vil.villagers[i];
    if (v.inside) continue;
    const Mesh* bubble = nullptr;
    if (v.state == VState::Panic || v.state == VState::Cower || v.held ||
        v.state == VState::Airborne)
      bubble = &bubbleFearMesh;
    else if (v.hunger > 0.72f)
      bubble = &bubbleHungerMesh;
    else if (v.energy < 0.22f)
      bubble = &bubbleSleepMesh;
    if (!bubble) continue;
    float bob = 0.08f * std::sin(time * 2.2f + static_cast<float>(i));
    glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                     v.pos + glm::vec3(0, 2.35f * v.scale + bob, 0)) *
                      glm::rotate(glm::mat4(1.0f), cam.yaw, glm::vec3(0, 1, 0)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(0.6f));
    lit.set("uModel", model);
    lit.set("uAlpha", 0.9f);
    bubble->draw();
  }

  if (vil.founded) {
    glm::vec3 fire = vil.campfirePos();
    // Flame after dusk, flickering.
    if (night > 0.2f) {
      float flick = 0.85f + 0.18f * std::sin(time * 23.7f) * std::sin(time * 13.1f + 1.7f);
      lit.set("uModel", glm::translate(glm::mat4(1.0f), fire) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(flick, flick * 1.1f, flick)));
      lit.set("uAlpha", 0.95f);
      flameMesh.draw();
    }
    // Smoke column - the "your village is alive over there" beacon.
    for (int k = 0; k < 4; ++k) {
      float yo = std::fmod(time * 0.9f + static_cast<float>(k) * 1.25f, 5.0f);
      float alpha = 0.28f * (1.0f - yo / 5.0f);
      if (alpha < 0.02f) continue;
      glm::vec3 p = fire + glm::vec3(0.4f * std::sin(time * 0.7f + yo), 1.7f + yo,
                                     0.3f * std::cos(time * 0.6f + yo));
      lit.set("uModel", glm::translate(glm::mat4(1.0f), p) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.5f + yo * 0.32f)));
      lit.set("uAlpha", alpha);
      smokeDisc.draw();
    }
    // Window glow after dark.
    if (night > 0.35f) {
      lit.set("uAlpha", std::min(1.0f, night * 1.3f));
      for (const Building& bd : vil.buildings) {
        if (bd.type != BuildingType::House || bd.stage != 3) continue;
        lit.set("uModel", glm::translate(glm::mat4(1.0f), bd.pos) *
                              glm::rotate(glm::mat4(1.0f), bd.yaw, glm::vec3(0, 1, 0)));
        houseWindows.draw();
      }
    }
  }

  // Influence rings: where the god's hand may act.
  {
    lit.set("uModel", glm::mat4(1.0f));
    lit.set("uAlpha", 0.26f + 0.06f * std::sin(time * 1.8f));
    if (templeRing.valid()) templeRing.draw();
    if (villageRing.valid()) villageRing.draw();
  }

  // Prayer motes above dancing worshippers, and miracle cast pulses.
  lit.set("uTint", glm::vec3(0.98f, 0.82f, 0.38f));
  for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
    const Villager& v = vil.villagers[i];
    if (!(v.job == Job::Worshipper && v.state == VState::Work) || v.inside) continue;
    for (int k = 0; k < 2; ++k) {
      float cycle = 2.4f;
      float yo = std::fmod(time * 1.1f + static_cast<float>(k) * 1.2f +
                               static_cast<float>(i) * 0.37f,
                           cycle);
      float frac = yo / cycle;
      glm::vec3 p = v.pos + glm::vec3(0.35f * std::sin(time * 1.3f + static_cast<float>(i + k)),
                                      1.7f * v.scale + yo * 1.3f,
                                      0.35f * std::cos(time * 1.1f + static_cast<float>(i)));
      lit.set("uModel", glm::translate(glm::mat4(1.0f), p) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.16f)));
      lit.set("uAlpha", 0.55f * (1.0f - frac));
      smokeDisc.draw();
    }
  }
  for (const CastEffect& e : effects) {
    float frac = e.age / 1.2f;
    float y = world.terrain.heightAt(e.pos.x, e.pos.z) + 0.3f;
    lit.set("uModel", glm::translate(glm::mat4(1.0f), glm::vec3(e.pos.x, y, e.pos.z)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(2.0f + frac * 22.0f)));
    lit.set("uAlpha", 0.55f * (1.0f - frac));
    smokeDisc.draw();
  }
  lit.set("uTint", glm::vec3(1.0f));

  lit.set("uAlpha", 1.0f);
  lit.set("uEmissive", 0.0f);
  gl.DepthMask(GL_TRUE);
  gl.Disable(GL_BLEND);

  water.draw(vp, camPos, sunDir, sunColor, fogColor, fogDensity, time);

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
  // Outside the god's influence the hand turns ghostly - look, don't touch.
  const bool reach = !hand.hasGround || world.insideInfluence(hand.groundPoint);
  lit.set("uTint", reach ? glm::vec3(1.0f) : glm::vec3(0.55f, 0.62f, 0.82f));
  lit.set("uAlpha", reach ? 0.95f : 0.45f);
  lit.set("uEmissive", 0.35f);
  const bool closed = hand.mode == Hand::Mode::Carry || panning;
  (closed ? handClosed : handOpen).draw();
  lit.set("uTint", glm::vec3(1.0f));
  lit.set("uAlpha", 1.0f);
  lit.set("uEmissive", 0.0f);
  gl.DepthMask(GL_TRUE);
  gl.Disable(GL_BLEND);

  SDL_GL_SwapWindow(window);
}

int App::runInteractive() {
  SDL_Log("Controls:");
  SDL_Log("  Left-drag ground     pan (grab the land)");
  SDL_Log("  Left-drag thing      pick up rock/tree/log/VILLAGER");
  SDL_Log("  ...release slowly    set down (on trees/field/water/site = assign job)");
  SDL_Log("  ...release moving    throw");
  SDL_Log("  Right/middle-drag    rotate & tilt camera");
  SDL_Log("  Mouse wheel          zoom toward cursor");
  SDL_Log("  WASD/arrows Q E      move / rotate, Shift = faster");
  SDL_Log("  M                    food miracle at the cursor (30 mana, inside influence)");
  SDL_Log("  Drop a villager on the totem to make a Worshipper - worship fills your mana");
  SDL_Log("  R new island   T advance time   F2 wireframe   Esc quit");
  SDL_Log("  Debug: F3 state tint   F4 sim speed   K spawn villager   L +10 res");

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
      char title[160];
      std::snprintf(title, sizeof(title),
                    "godgame - %.0f fps | pop %d  wood %d  food %d | mana %.0f  "
                    "belief %.0f%% | day %.2f",
                    fpsFrames / fpsTimer, world.village.population(),
                    world.village.wood, world.village.food, world.temple.mana,
                    world.village.belief * 100.0f, world.dayCycle.t);
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
  } else if (view == "village" || view == "night") {
    cam.focus = world.village.center;
    cam.distance = 85.0f;
    cam.yaw = 2.3f;
    if (view == "night") world.dayCycle.t = 0.93f;
  } else if (view == "temple") {
    cam.focus = world.temple.pos;
    cam.distance = 55.0f;
    cam.yaw = world.temple.yaw + 3.14159f;
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

// ------------------------------------------------------------ headless tests

std::uint64_t fnvMix(std::uint64_t h, const void* data, std::size_t len) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

std::uint64_t worldChecksum(const World& w) {
  std::uint64_t h = 1469598103934665603ull;
  auto addF = [&](float f) {
    auto q = static_cast<std::int64_t>(std::llround(static_cast<double>(f) * 1000.0));
    h = fnvMix(h, &q, sizeof q);
  };
  for (const Villager& v : w.village.villagers) {
    addF(v.pos.x);
    addF(v.pos.y);
    addF(v.pos.z);
    addF(v.hunger);
    int s = static_cast<int>(v.state), j = static_cast<int>(v.job);
    h = fnvMix(h, &s, sizeof s);
    h = fnvMix(h, &j, sizeof j);
  }
  int counters[3] = {w.village.wood, w.village.food, w.village.population()};
  h = fnvMix(h, counters, sizeof counters);
  addF(w.village.belief);
  addF(w.temple.mana);
  addF(w.dayCycle.t);
  return h;
}

int runHeadless(std::uint32_t seed, int steps) {
  const float dt = 1.0f / 60.0f;
  bool allOk = true;
  auto check = [&](bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    allOk &= ok;
  };

  World world;
  world.generate(seed);
  Village& vil = world.village;

  std::printf("seed          %u\n", seed);
  std::printf("land fraction %.3f\n", world.terrain.landFraction());
  std::printf("height range  %.2f .. %.2f\n", world.terrain.minHeight(),
              world.terrain.maxHeight());
  std::size_t trees = 0, rocks = 0;
  for (const Prop& p : world.props) {
    if (p.type == PropType::Tree) ++trees;
    if (p.type == PropType::Rock) ++rocks;
  }
  std::printf("props         %zu trees, %zu rocks\n", trees, rocks);
  std::printf("village       (%.0f, %.0f) h=%.1f | pop %d | %zu buildings | %zu fishing spots\n",
              vil.center.x, vil.center.z, vil.center.y, vil.population(),
              vil.buildings.size(), vil.fishingSpots.size());

  std::printf("[1] world & village layout\n");
  check(world.terrain.landFraction() > 0.15f && world.terrain.landFraction() < 0.8f,
        "island land fraction sane");
  check(trees > 50 && rocks > 30, "props scattered");
  check(vil.founded, "village founded");
  check(world.terrain.heightAt(vil.center.x, vil.center.z) > 1.5f, "village on land");
  check(world.terrain.normalAt(vil.center.x, vil.center.z).y > 0.9f, "terrace is flat");
  check(!vil.fishingSpots.empty(), "found a fishing spot");
  check(vil.population() == tune::kStartPopulation, "starting population spawned");

  // [2] The original physics regression: hurl a rock, it flies, lands, sleeps.
  std::printf("[2] thrown rock\n");
  int rockIndex = -1;
  for (std::size_t i = 0; i < world.props.size(); ++i)
    if (world.props[i].type == PropType::Rock) {
      rockIndex = static_cast<int>(i);
      break;
    }
  check(rockIndex >= 0, "a rock exists");
  if (rockIndex >= 0) {
    Prop& rock = world.props[rockIndex];
    rock.pos = glm::vec3(0.0f, world.terrain.heightAt(0.0f, 0.0f) + 30.0f, 0.0f);
    world.throwProp(rockIndex, glm::vec3(18.0f, 6.0f, 11.0f));
    glm::vec3 start = rock.pos;
    for (int i = 0; i < steps; ++i) world.update(dt);
    bool finite = std::isfinite(rock.pos.x) && std::isfinite(rock.pos.y) &&
                  std::isfinite(rock.pos.z);
    float travelled = glm::distance(glm::vec2(start.x, start.z),
                                    glm::vec2(rock.pos.x, rock.pos.z));
    std::printf("      travelled %.1f, final (%.1f, %.1f, %.1f), asleep=%d\n", travelled,
                rock.pos.x, rock.pos.y, rock.pos.z, rock.asleep ? 1 : 0);
    check(finite && travelled > 5.0f && rock.asleep, "flies, lands, sleeps");
  }

  // [3] Hand script: throw a villager hard - flail, stun, panic, recover.
  //     Villagers are invulnerable this slice; this test flips when mortality lands.
  std::printf("[3] thrown villager\n");
  {
    Villager& v = vil.villagers[0];
    v.pos = vil.center + glm::vec3(0.0f, 14.0f, 0.0f);
    villagerReleased(world, 0, glm::vec3(24.0f, 6.0f, 13.0f), false);
    bool sawAirborne = false, sawStunned = false, sawRecovered = false;
    for (int i = 0; i < 3000 && !sawRecovered; ++i) {
      world.update(dt);
      VState s = vil.villagers[0].state;
      sawAirborne |= s == VState::Airborne;
      sawStunned |= s == VState::Stunned;
      if (sawStunned && (s == VState::Idle || s == VState::Wander || s == VState::Panic))
        sawRecovered = true;
    }
    const Villager& v0 = vil.villagers[0];
    bool finite = std::isfinite(v0.pos.x) && std::isfinite(v0.pos.y) &&
                  std::isfinite(v0.pos.z);
    check(sawAirborne, "went airborne");
    check(sawStunned, "hard landing stunned");
    check(sawRecovered, "got up and recovered");
    check(finite, "position stayed finite");
    // Maximum violence, still alive (the invulnerability tripwire).
    vil.villagers[0].pos = vil.center + glm::vec3(0.0f, 60.0f, 0.0f);
    villagerReleased(world, 0, glm::vec3(65.0f, 0.0f, 0.0f), false);
    for (int i = 0; i < 3000; ++i) world.update(dt);
    const Villager& v1 = vil.villagers[0];
    check(std::isfinite(v1.pos.x) && std::isfinite(v1.pos.y), "survived 65 m/s (invulnerable)");
  }

  // [4] Drop-to-assign resolution rules.
  std::printf("[4] drop-to-assign\n");
  {
    glm::vec3 fieldP(vil.fieldCenter.x, 0.0f, vil.fieldCenter.y);
    fieldP.y = world.terrain.heightAt(fieldP.x, fieldP.z);
    check(vil.resolveJobAtPoint(world, fieldP) == Job::Farmer, "field -> farmer");

    int siteIdx = -1;
    for (std::size_t b = 0; b < vil.buildings.size(); ++b)
      if (vil.buildings[b].type == BuildingType::House && vil.buildings[b].stage == 0)
        siteIdx = static_cast<int>(b);
    check(siteIdx >= 0, "a construction site is open at start");
    if (siteIdx >= 0)
      check(vil.resolveJobAtPoint(world, vil.buildings[siteIdx].pos) == Job::Builder,
            "site -> builder");

    int treeIdx = -1;
    for (std::size_t i = 0; i < world.props.size(); ++i)
      if (world.props[i].alive && world.props[i].type == PropType::Tree) {
        treeIdx = static_cast<int>(i);
        break;
      }
    if (treeIdx >= 0)
      check(vil.resolveJobAtPoint(world, world.props[treeIdx].pos) == Job::Forester,
            "tree -> forester");

    glm::vec3 waterP = vil.fishingSpots.empty()
                           ? glm::vec3(0.0f)
                           : vil.fishingSpots[0];
    // Push past the spot to actual water.
    glm::vec3 out = glm::normalize(glm::vec3(waterP.x - vil.center.x, 0.0f,
                                             waterP.z - vil.center.z));
    for (float d = 0.0f; d < 40.0f; d += 2.0f) {
      glm::vec3 p = waterP + out * d;
      if (world.terrain.heightAt(p.x, p.z) < Terrain::WATER_LEVEL) {
        waterP = p;
        break;
      }
    }
    check(vil.resolveJobAtPoint(world, waterP) == Job::Fisherman, "water -> fisherman");

    // Full gentle-placement path: set a jobless villager down on the field.
    Villager& v = vil.villagers[5];
    v.pos = fieldP + glm::vec3(0.5f, 1.0f, 0.5f);
    villagerReleased(world, 5, glm::vec3(0.3f, 0.0f, 0.2f), true);
    for (int i = 0; i < 240; ++i) world.update(dt);
    check(vil.villagers[5].job == Job::Farmer, "gently placed on field -> becomes farmer");
  }

  // [5] Resources dropped on the storage pad are absorbed. (Builders may be
  // withdrawing concurrently, so compare the monotonic produced-counter.)
  std::printf("[5] storage absorption\n");
  {
    int producedBefore = vil.woodProduced;
    Prop log;
    log.type = PropType::Log;
    log.scale = 1.0f;
    log.radius = 0.5f;
    log.resource = 1.0f;
    log.pos = vil.storagePos() + glm::vec3(0.0f, 3.0f, 0.0f);
    log.asleep = false;
    world.spawnProp(log);
    for (int i = 0; i < 300; ++i) world.update(dt);
    check(vil.woodProduced == producedBefore + 1, "log dropped on storage -> +1 wood");
  }

  // [8 first, so the fresh soak worlds below stay unpolluted]
  // Worship, mana, influence and the food miracle.
  std::printf("[8] worship, mana & the temple\n");
  {
    check(world.temple.founded, "temple founded");
    float distTV = glm::distance(glm::vec2(world.temple.pos.x, world.temple.pos.z),
                                 glm::vec2(vil.center.x, vil.center.z));
    std::printf("      temple at (%.0f, %.0f), %.0f m from the village | mana %.0f | belief %.2f\n",
                world.temple.pos.x, world.temple.pos.z, distTV, world.temple.mana,
                vil.belief);
    check(distTV > 40.0f && distTV < 70.0f, "temple stands apart, near the village");
    check(world.temple.pos.y > 1.0f, "temple on land");
    check(vil.resolveJobAtPoint(world, vil.buildings[vil.centerIdx].pos) ==
              Job::Worshipper,
          "totem -> worshipper");
    check(world.insideInfluence(vil.center), "village inside influence");
    check(world.insideInfluence(world.temple.pos), "temple inside influence");

    glm::vec2 c2(vil.center.x, vil.center.z);
    glm::vec2 awayDir = glm::length(c2) > 1.0f ? -glm::normalize(c2)
                                               : glm::vec2(0.7071f, 0.7071f);
    glm::vec3 farPoint(awayDir.x * Terrain::SIZE * 0.45f, 0.0f,
                       awayDir.y * Terrain::SIZE * 0.45f);
    check(!world.insideInfluence(farPoint), "far shore outside influence");

    world.temple.mana = 5.0f;
    check(!world.castFoodMiracle(vil.center), "cast fails without mana");
    world.temple.mana = 100.0f;
    check(!world.castFoodMiracle(farPoint), "cast fails outside influence");

    float beliefBefore = vil.belief;
    auto countFood = [&]() {
      int n = 0;
      for (const Prop& p : world.props)
        if (p.alive && p.type == PropType::Food) ++n;
      return n;
    };
    int foodBefore = countFood();
    check(world.castFoodMiracle(vil.center + glm::vec3(5.0f, 0.0f, 5.0f)),
          "cast succeeds inside influence");
    check(world.temple.mana == 100.0f - tune::kFoodMiracleCost, "mana was spent");
    check(countFood() >= foodBefore + tune::kFoodMiracleBundles, "food rained from heaven");
    check(vil.belief > beliefBefore, "witnesses believed harder");

    // A fresh world: does the starter worshipper alone fill the pool?
    World w3;
    w3.generate(seed);
    w3.dayCycle.secondsPerDay = 240.0f;
    float manaStart = w3.temple.mana;
    bool dancerSeen = false;
    for (int i = 0; i < static_cast<int>(240.0f / dt); ++i) {
      w3.update(dt);
      if ((i & 127) == 0 && w3.village.activeWorshippers() > 0) dancerSeen = true;
    }
    std::printf("      one day of worship: mana %.0f (from %.0f), belief %.2f\n",
                w3.temple.mana, manaStart, w3.village.belief);
    check(dancerSeen, "the worshipper danced at the totem");
    check(w3.temple.mana > manaStart + 5.0f, "worship generated mana");
  }

  // [6] Three-day economy & schedule soak. Days are shrunk to 240 s - short
  // enough to simulate fast, long enough that walking/chopping (real-time
  // actions) still fit inside a work day.
  std::printf("[6] three-day soak\n");
  {
    World w2;
    w2.generate(seed);
    w2.dayCycle.secondsPerDay = 240.0f;
    Village& v2 = w2.village;
    int steps3d = static_cast<int>(3.0f * 240.0f / dt);
    float midnightSleep = -1.0f, noonActive = -1.0f;
    bool finite = true, inBounds = true;
    float prevT = w2.dayCycle.t;
    for (int i = 0; i < steps3d; ++i) {
      w2.update(dt);
      float t = w2.dayCycle.t;
      if (prevT > t) {  // wrapped midnight: census
        int asleep = 0;
        for (const Villager& v : v2.villagers)
          if (v.state == VState::Sleep || v.inside) ++asleep;
        midnightSleep = static_cast<float>(asleep) /
                        static_cast<float>(v2.villagers.size());
      }
      if (prevT < 0.5f && t >= 0.5f) {  // noon census
        int active = 0;
        for (const Villager& v : v2.villagers)
          if (!v.inside && v.state != VState::Sleep) ++active;
        noonActive = static_cast<float>(active) /
                     static_cast<float>(v2.villagers.size());
      }
      prevT = t;
      if ((i & 255) == 0) {
        for (const Villager& v : v2.villagers) {
          finite &= std::isfinite(v.pos.x) && std::isfinite(v.pos.y) &&
                    std::isfinite(v.pos.z);
          inBounds &= std::abs(v.pos.x) < Terrain::SIZE * 0.55f &&
                      std::abs(v.pos.z) < Terrain::SIZE * 0.55f;
        }
      }
    }
    int stage3Houses = 0;
    for (const Building& b : v2.buildings)
      if (b.type == BuildingType::House && b.stage == 3) ++stage3Houses;
    std::printf(
        "      wood %d (produced %d) | food %d (produced %d, eaten %d) | pop %d | "
        "houses %d | stuck %d | midnight asleep %.0f%% | noon active %.0f%% | "
        "mana %.0f | belief %.2f\n",
        v2.wood, v2.woodProduced, v2.food, v2.foodProduced, v2.mealsEaten,
        v2.population(), stage3Houses, v2.stuckEvents, midnightSleep * 100.0f,
        noonActive * 100.0f, w2.temple.mana, v2.belief);
    check(w2.temple.mana > tune::kManaStart, "worship filled the mana pool");
    check(finite, "all positions finite");
    check(inBounds, "everyone stayed on the island");
    check(v2.woodProduced > 0, "wood was produced");
    check(v2.foodProduced > 0, "food was produced");
    check(v2.mealsEaten > 0, "meals were eaten");
    check(v2.food >= 0 && v2.wood >= 0, "stores never went negative");
    check(v2.population() > tune::kStartPopulation, "population grew");
    check(stage3Houses > 3, "the starter construction site was completed");
    check(v2.stuckEvents < 60, "stuck watchdog under control");
    check(midnightSleep >= 0.7f, "village sleeps at midnight");
    check(noonActive >= 0.6f, "village is active at noon");
  }

  // [7] Determinism: same seed, same steps, identical checksums.
  std::printf("[7] determinism\n");
  {
    World a, b;
    a.generate(seed);
    b.generate(seed);
    a.dayCycle.secondsPerDay = 60.0f;
    b.dayCycle.secondsPerDay = 60.0f;
    for (int i = 0; i < 2000; ++i) {
      a.update(dt);
      b.update(dt);
    }
    std::uint64_t ha = worldChecksum(a), hb = worldChecksum(b);
    std::printf("      checksum %016llx\n", static_cast<unsigned long long>(ha));
    check(ha == hb, "two runs match bit-for-bit");
  }

  std::printf(allOk ? "OK\n" : "FAIL\n");
  return allOk ? 0 : 1;
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
