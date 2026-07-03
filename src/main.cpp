// godgame - a Black & White inspired god game (first slice: land, camera, hand).
//
// Modes:
//   godgame                          interactive skirmish (a rival god plays too)
//   godgame --no-rival               interactive sandbox, no opponent
//   godgame --seed 1234              a specific island seed
//   godgame --headless [steps]       no window; generate + simulate + self-test
//   godgame --match [days]           no window; AI-vs-AI skirmish, day-by-day report
//   godgame --screenshot out.bmp [frames] [far|close|village|night|temple|rival|roster]

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

// A terrain-following band at `radius` around `center` - the influence ring,
// colored for whichever god projects it.
MeshData buildRingMeshData(const Terrain& terrain, glm::vec2 center, float radius,
                           glm::vec3 color = glm::vec3(1.0f, 0.88f, 0.45f)) {
  MeshData md;
  const int kSegments = 120;
  const float kHalfWidth = 0.55f;
  for (int s = 0; s <= kSegments; ++s) {
    float a = 6.2831853f * static_cast<float>(s) / kSegments;
    glm::vec2 dir(std::sin(a), std::cos(a));
    for (float r : {radius - kHalfWidth, radius + kHalfWidth}) {
      glm::vec2 p = center + dir * r;
      float y = std::max(terrain.heightAt(p.x, p.y), Terrain::WATER_LEVEL) + 0.18f;
      md.addVertex(glm::vec3(p.x, y, p.y), glm::vec3(0, 1, 0), color);
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

// Each god's identity color (rings, totem accents, the rival's hand).
// Neutral = unpainted.
glm::vec3 godColor(int god) {
  if (god == 0) return {1.0f, 0.88f, 0.45f};   // the player: gold
  if (god == 1) return {0.95f, 0.35f, 0.30f};  // the rival: crimson
  return {1.0f, 1.0f, 1.0f};
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
  Mesh templeRings[tune::kMaxGods];
  std::vector<Mesh> villageRings;
  std::vector<float> lastRingRadii;
  std::vector<int> lastRingOwners;
  Mesh largeAbodeMesh, workshopMesh, storeMesh, crecheMesh, graveyardMesh;
  Mesh dispenserMesh, wonderMesh, scaffoldMesh;
  Mesh bodyMeshes[3];

  // Short-lived cast feedback (expanding gold pulse at miracle points).
  struct CastEffect {
    glm::vec3 pos;
    float age;
  };
  std::vector<CastEffect> effects;
  std::vector<int> lastOwners;  // detects conversion ceremonies
  bool wasBroken[tune::kMaxGods] = {};  // detects defeat / victory
  bool rivalEnabled = true;             // --no-rival reverts to the sandbox

  const Mesh* buildingMesh(BuildingType t);
  void refreshVillageRings();

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
  largeAbodeMesh.upload(models::largeAbode());
  workshopMesh.upload(models::workshop());
  storeMesh.upload(models::store());
  crecheMesh.upload(models::creche());
  graveyardMesh.upload(models::graveyard());
  dispenserMesh.upload(models::dispenser());
  wonderMesh.upload(models::wonder());
  scaffoldMesh.upload(models::scaffoldProp());
  for (int v = 0; v < 3; ++v) bodyMeshes[v].upload(models::bodyProp(v));

  rebuildWorld(seed);
}

// The finished look of each buildable type (ghost previews reuse this).
const Mesh* App::buildingMesh(BuildingType t) {
  switch (t) {
    case BuildingType::House: return &houseStages[3];
    case BuildingType::LargeAbode: return &largeAbodeMesh;
    case BuildingType::Store: return &storeMesh;
    case BuildingType::Workshop: return &workshopMesh;
    case BuildingType::Creche: return &crecheMesh;
    case BuildingType::Graveyard: return &graveyardMesh;
    case BuildingType::FieldSite: return &fieldSlabMesh;
    case BuildingType::Center: return &totemMesh;
    case BuildingType::Dispenser: return &dispenserMesh;
    case BuildingType::Wonder: return &wonderMesh;
    default: return nullptr;
  }
}

void App::rebuildWorld(std::uint32_t newSeed) {
  seed = newSeed;
  world.generate(seed, rivalEnabled ? 2 : 1);
  terrainMesh.upload(world.terrain.buildMeshData());
  hand.mode = Hand::Mode::Free;
  hand.held.clear();
  hand.hover.clear();
  effects.clear();
  for (int g = 0; g < tune::kMaxGods; ++g) {
    wasBroken[g] = false;
    const Temple& t = world.gods[g].temple;
    if (world.gods[g].active && t.founded)
      templeRings[g].upload(buildRingMeshData(world.terrain,
                                              glm::vec2(t.pos.x, t.pos.z),
                                              tune::kTempleInfluence, godColor(g)));
    else
      templeRings[g] = Mesh{};
  }
  villageRings.clear();
  villageRings.resize(world.villages.size());
  lastRingRadii.assign(world.villages.size(), -1.0f);
  lastRingOwners.assign(world.villages.size(), -2);
  refreshVillageRings();
  int totalPop = 0;
  for (const Village& v : world.villages) totalPop += v.population();
  SDL_Log("World seed %u | land %.0f%% | height %.1f..%.1f | %zu props | %zu villages, pop %d%s",
          seed, world.terrain.landFraction() * 100.0f, world.terrain.minHeight(),
          world.terrain.maxHeight(), world.props.size(), world.villages.size(),
          totalPop, world.gods[1].active ? " | a rival god stirs" : "");
}

// Owned villages project their god's rings, growing with belief; rebuild each
// ring's terrain-following mesh only when its radius or owner truly changes.
void App::refreshVillageRings() {
  for (std::size_t v = 0; v < world.villages.size(); ++v) {
    const Village& vil = world.villages[v];
    if (!vil.founded || vil.owner < 0) {
      lastRingRadii[v] = -1.0f;
      lastRingOwners[v] = vil.owner;
      villageRings[v] = Mesh{};
      continue;
    }
    float r = vil.influenceRadius();
    if (std::abs(r - lastRingRadii[v]) > 0.75f || lastRingOwners[v] != vil.owner) {
      lastRingRadii[v] = r;
      lastRingOwners[v] = vil.owner;
      villageRings[v].upload(buildRingMeshData(
          world.terrain, glm::vec2(vil.center.x, vil.center.z), r,
          godColor(vil.owner)));
    }
  }
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
      // Holding a 3-stack: the wheel picks the civic building instead of zooming.
      if (hand.heldScaffoldCount(world) == 3 && e.wheel.y != 0) {
        static const BuildingType kCivic[4] = {
            BuildingType::Store, BuildingType::Workshop, BuildingType::Creche,
            BuildingType::Graveyard};
        int cur = 0;
        for (int k = 0; k < 4; ++k)
          if (kCivic[k] == hand.civicChoice) cur = k;
        cur = (cur + (e.wheel.y > 0 ? 1 : 3)) % 4;
        hand.civicChoice = kCivic[cur];
      } else {
        wheelAccum += static_cast<float>(e.wheel.y);
      }
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
          if (hand.hasGround && !world.villages.empty()) {
            Villager v;
            v.pos = hand.groundPoint;
            v.pos.y = world.terrain.heightAt(v.pos.x, v.pos.z);
            v.rng = seed ^ (static_cast<std::uint32_t>(world.home().villagers.size()) *
                            2654435761u);
            v.variant = static_cast<int>(world.home().villagers.size() % 3);
            world.home().villagers.push_back(v);
          }
          break;
        case SDLK_l:
          world.home().wood += 10;
          world.home().food += 10;
          break;
        case SDLK_m:
          if (hand.hasGround) {
            if (world.castFoodMiracle(hand.groundPoint)) {
              effects.push_back({hand.groundPoint, 0.0f});
              SDL_Log("food miracle! mana %.0f/%.0f", world.gods[0].mana,
                      world.gods[0].manaMax);
            } else if (!world.insideInfluence(hand.groundPoint)) {
              SDL_Log("cannot cast: outside your influence");
            } else {
              SDL_Log("cannot cast: need %.0f mana (have %.0f)",
                      tune::kFoodMiracleCost, world.gods[0].mana);
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

  refreshVillageRings();

  // Conversion ceremonies: a pulse and a headline when a village changes gods.
  if (lastOwners.size() != world.villages.size())
    lastOwners.assign(world.villages.size(), -2);
  for (std::size_t v = 0; v < world.villages.size(); ++v) {
    int owner = world.villages[v].owner;
    if (lastOwners[v] != -2 && lastOwners[v] != owner) {
      effects.push_back({world.villages[v].center, 0.0f});
      SDL_Log(owner == 0 ? "A village has joined your faith!"
                         : "A village has fallen to the rival god!");
    }
    lastOwners[v] = owner;
  }

  // Defeat and victory (the full ceremony arrives with the skirmish shell).
  for (int g = 0; g < tune::kMaxGods; ++g) {
    bool broken = world.godBroken(g);
    if (broken && !wasBroken[g]) {
      if (g == 0)
        SDL_Log("Your last village has fallen. The island forgets you...");
      else
        SDL_Log("The rival god is broken - its hand hangs still. The island is yours!");
    }
    wasBroken[g] = broken;
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
      case PropType::Scaffold: {
        // A stack draws the lattice unit once per level.
        int count = std::clamp(static_cast<int>(std::lround(p.resource)), 1,
                               tune::kMaxScaffoldStack);
        for (int k = 0; k < count; ++k) {
          lit.set("uModel", model * glm::translate(glm::mat4(1.0f),
                                                   glm::vec3(0, 1.35f * k, 0)));
          scaffoldMesh.draw();
        }
        break;
      }
      case PropType::Body: {
        // The dead grey as they rot; burial is overdue when they look it.
        float rot = std::clamp(
            p.age / (tune::kCorpseRotDays * world.dayCycle.secondsPerDay), 0.0f,
            1.0f);
        lit.set("uTint", glm::mix(glm::vec3(1.0f), glm::vec3(0.52f, 0.56f, 0.48f),
                                  rot * 0.8f));
        bodyMeshes[p.variant % 3].draw();
        lit.set("uTint", glm::vec3(1.0f));
        break;
      }
    }
    if (mesh) mesh->draw();
  }
  lit.set("uEmissive", 0.0f);

  // The temples: each god's seat, crystals glowing with stored mana, the
  // rival's stonework washed in its color.
  for (int g = 0; g < tune::kMaxGods; ++g) {
    const God& deity = world.gods[g];
    if (!deity.active || !deity.temple.founded) continue;
    glm::mat4 tm = glm::translate(glm::mat4(1.0f), deity.temple.pos) *
                   glm::rotate(glm::mat4(1.0f), deity.temple.yaw, glm::vec3(0, 1, 0));
    lit.set("uModel", tm);
    if (g != 0)
      lit.set("uTint", glm::mix(glm::vec3(1.0f), godColor(g), 0.35f));
    templeMesh.draw();
    float manaFrac = deity.manaMax > 0.0f ? deity.mana / deity.manaMax : 0.0f;
    // The beacon floats above the roof so the mana level reads from anywhere.
    lit.set("uEmissive",
            0.30f + 0.60f * manaFrac + 0.05f * std::sin(time * 3.1f));
    lit.set("uModel", tm * glm::translate(glm::mat4(1.0f),
                                          glm::vec3(0, 4.9f + 0.25f * std::sin(time * 1.1f), 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(1.25f)));
    templeCrystalMesh.draw();
    lit.set("uEmissive", 0.0f);
    lit.set("uTint", glm::vec3(1.0f));
  }

  // Villages: buildings, stock piles, fields - owned and neutral alike.
  for (const Village& vil : world.villages) {
  if (!vil.founded) continue;
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
        // The totem wears its god's color - repainted by conversion.
        lit.set("uTint", vil.owner >= 0
                             ? glm::mix(glm::vec3(1.0f), godColor(vil.owner), 0.45f)
                             : glm::vec3(1.0f));
        lit.set("uModel", model * glm::scale(glm::mat4(1.0f),
                                             glm::vec3(1.0f + 0.14f * (bd.level - 1))));
        totemMesh.draw();
        lit.set("uTint", glm::vec3(1.0f));
        lit.set("uEmissive", 0.0f);
        break;
      case BuildingType::Storage: storagePadMesh.draw(); break;
      case BuildingType::Campfire: campfireMesh.draw(); break;
      case BuildingType::House:
        houseStages[std::clamp(bd.stage, 0, 3)].draw();
        break;
      default: {
        if (bd.stage >= 3) {
          const Mesh* m = buildingMesh(bd.type);
          if (m) m->draw();
          // Graves accumulate as little stone cairns.
          if (bd.type == BuildingType::Graveyard && bd.charges > 0) {
            static const glm::vec2 kGraveSpots[8] = {
                {-1.4f, -1.2f}, {0.2f, -0.9f}, {1.5f, -1.4f}, {-0.9f, 0.2f},
                {1.2f, 0.6f},   {-1.6f, 1.3f}, {0.3f, 1.5f},  {1.7f, 1.6f}};
            for (int g = 0; g < std::min(bd.charges, 8); ++g) {
              lit.set("uModel",
                      model * glm::translate(glm::mat4(1.0f),
                                             glm::vec3(kGraveSpots[g].x, 0.15f,
                                                       kGraveSpots[g].y)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.30f)));
              rockMeshes[g % 3].draw();
            }
          }
        } else if (bd.tier > 0) {
          // Under construction: the scaffold stack stands at the site.
          for (int k = 0; k < bd.tier; ++k) {
            lit.set("uModel", model * glm::translate(glm::mat4(1.0f),
                                                     glm::vec3(0, 1.35f * k, 0)));
            scaffoldMesh.draw();
          }
        } else {
          houseStages[std::clamp(bd.stage, 0, 2)].draw();
        }
        break;
      }
    }
  }

  {
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
    // The fields and their crops.
    for (const Field& f : vil.fields) {
      float fh = world.terrain.heightAt(f.center.x, f.center.y);
      lit.set("uModel", glm::translate(glm::mat4(1.0f),
                                       glm::vec3(f.center.x, fh, f.center.y)));
      fieldSlabMesh.draw();
    }
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

  }  // per-village buildings/piles/fields

  // Villagers: six posed parts each, two at distance.
  for (std::size_t vIdx = 0; vIdx < world.villages.size(); ++vIdx) {
  const Village& vil = world.villages[vIdx];
  for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
    const Villager& v = vil.villagers[i];
    if (!v.alive || v.inside) continue;
    VillagerPose pose = computeVillagerPose(v, time);
    glm::mat4 root = glm::translate(glm::mat4(1.0f), v.pos) * pose.root;
    bool farAway = glm::distance(camPos, v.pos) > 180.0f;
    bool hovered = hand.hover.isVillager() &&
                   hand.hover.village == static_cast<int>(vIdx) &&
                   hand.hover.index == static_cast<int>(i);
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
  }  // per-village villagers
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
  for (const Village& vil : world.villages)
  for (const Villager& v : vil.villagers) {
    if (!v.alive || !(v.held || v.state == VState::Airborne)) continue;
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
  for (const Village& vil : world.villages)
  for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
    const Villager& v = vil.villagers[i];
    if (!v.alive || v.inside) continue;
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

  for (const Village& vil : world.villages) {
    if (!vil.founded) continue;
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

  // Influence rings: where each god's hand may act (gold = yours).
  {
    lit.set("uModel", glm::mat4(1.0f));
    lit.set("uAlpha", 0.26f + 0.06f * std::sin(time * 1.8f));
    for (const Mesh& ring : templeRings)
      if (ring.valid()) ring.draw();
    for (const Mesh& ring : villageRings)
      if (ring.valid()) ring.draw();
  }

  // Scaffold placement ghost: what this stack becomes, and whether it fits.
  {
    int heldStack = hand.heldScaffoldCount(world);
    if (heldStack > 0 && hand.hasGround && !world.villages.empty()) {
      const Village& vil = world.home();
      BuildingType t = Village::buildingForStack(heldStack, hand.civicChoice);
      bool valid = world.scaffoldPlacementValid(hand.groundPoint, heldStack);
      glm::vec3 gp = t == BuildingType::Center && vil.centerIdx >= 0
                         ? vil.buildings[vil.centerIdx].pos
                         : glm::vec3(hand.groundPoint.x,
                                     world.terrain.heightAt(hand.groundPoint.x,
                                                            hand.groundPoint.z),
                                     hand.groundPoint.z);
      glm::vec2 toCenter = glm::vec2(vil.center.x - gp.x, vil.center.z - gp.z);
      float gy = glm::length(toCenter) > 0.5f ? std::atan2(toCenter.x, toCenter.y) : 0.0f;
      const Mesh* m = buildingMesh(t);
      if (m) {
        lit.set("uTint", valid ? glm::vec3(0.55f, 1.0f, 0.55f)
                               : glm::vec3(1.0f, 0.40f, 0.40f));
        lit.set("uEmissive", 0.55f);
        lit.set("uAlpha", 0.40f);
        lit.set("uModel", glm::translate(glm::mat4(1.0f), gp) *
                              glm::rotate(glm::mat4(1.0f), gy, glm::vec3(0, 1, 0)));
        m->draw();
        lit.set("uTint", glm::vec3(1.0f));
        lit.set("uEmissive", 1.0f);
      }
    }
  }

  // Prayer motes above dancing worshippers, miracle cast pulses, and
  // dispenser charge orbs.
  lit.set("uTint", glm::vec3(0.98f, 0.82f, 0.38f));
  for (const Village& vil : world.villages)
  for (const Building& bd : vil.buildings) {
    if (bd.type != BuildingType::Dispenser || bd.stage != 3) continue;
    for (int k = 0; k < bd.charges; ++k) {
      float a = time * 1.1f + static_cast<float>(k) * 2.094f;
      glm::vec3 p = bd.pos + glm::vec3(std::cos(a) * 1.5f,
                                       2.6f + 0.18f * std::sin(time * 2.0f + k),
                                       std::sin(a) * 1.5f);
      lit.set("uModel", glm::translate(glm::mat4(1.0f), p) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.22f)));
      lit.set("uAlpha", 0.85f);
      smokeDisc.draw();
    }
  }
  for (const Village& vil : world.villages)
  for (std::size_t i = 0; i < vil.villagers.size(); ++i) {
    const Villager& v = vil.villagers[i];
    if (!v.alive || !(v.job == Job::Worshipper && v.state == VState::Work) ||
        v.inside)
      continue;
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

  // The rival's embodied hand: ghostly, its own color, readable from afar.
  gl.Enable(GL_BLEND);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.DepthMask(GL_FALSE);
  lit.use();
  for (int g = 0; g < tune::kMaxGods; ++g) {
    if (!world.gods[g].active || !world.gods[g].ai) continue;
    const GodAI& brain = world.ai[g];
    if (brain.handPos.y > 1.0e8f) continue;
    glm::vec2 v2(brain.handVel.x, brain.handVel.z);
    float yaw = glm::length(v2) > 1.0f ? std::atan2(v2.x, v2.y)
                                       : time * 0.35f;  // idle: slow menace
    glm::mat4 model = glm::translate(glm::mat4(1.0f), brain.handPos) *
                      glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
                      glm::rotate(glm::mat4(1.0f), -0.30f, glm::vec3(1, 0, 0)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(2.6f));
    lit.set("uModel", model);
    lit.set("uTint", godColor(g));
    lit.set("uAlpha", 0.55f);
    lit.set("uEmissive", 0.30f);
    (brain.held.none() ? handOpen : handClosed).draw();
  }
  lit.set("uTint", glm::vec3(1.0f));
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
      int pop = 0;
      for (const Village& v : world.villages) pop += v.population();
      std::snprintf(title, sizeof(title),
                    "godgame - %.0f fps | pop %d  wood %d  food %d | mana %.0f  "
                    "belief %.0f%% | day %.2f",
                    fpsFrames / fpsTimer, pop, world.home().wood,
                    world.home().food, world.gods[0].mana,
                    world.home().belief[0] * 100.0f, world.dayCycle.t);
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
    cam.focus = world.home().center;
    cam.distance = 85.0f;
    cam.yaw = 2.3f;
    if (view == "night") world.dayCycle.t = 0.93f;
  } else if (view == "temple") {
    cam.focus = world.gods[0].temple.pos;
    cam.distance = 55.0f;
    cam.yaw = world.gods[0].temple.yaw + 3.14159f;
  } else if (view == "rival") {
    // The rival god's home village (falls back to the second village).
    glm::vec3 focus = world.villages.size() > 1 ? world.villages[1].center
                                                : world.home().center;
    for (const Village& v : world.villages)
      if (v.founded && v.owner == 1) focus = v.center;
    cam.focus = focus;
    cam.distance = 95.0f;
    cam.yaw = 1.1f;
  } else if (view == "roster") {
    // A model-viewer scene: every scaffold-built building in a row, plus
    // scaffold stacks, so the whole roster can be eyeballed at once.
    Village& v = world.home();
    const BuildingType kTypes[] = {
        BuildingType::House,     BuildingType::LargeAbode, BuildingType::Store,
        BuildingType::Workshop,  BuildingType::Creche,     BuildingType::Graveyard,
        BuildingType::Dispenser, BuildingType::Wonder};
    glm::vec3 f(std::sin(2.3f), 0.0f, std::cos(2.3f));
    glm::vec3 right(-f.z, 0.0f, f.x);
    for (int k = 0; k < 8; ++k) {
      Building b;
      b.type = kTypes[k];
      b.pos = v.center + right * (static_cast<float>(k - 4) * 11.0f) + f * 26.0f;
      // Pull anything that landed in the sea back toward the village.
      for (int step = 0; step < 24 && world.terrain.heightAt(b.pos.x, b.pos.z) < 1.8f;
           ++step)
        b.pos = glm::mix(b.pos, v.center, 0.12f);
      b.pos.y = world.terrain.heightAt(b.pos.x, b.pos.z);
      b.yaw = 2.3f + 3.14159f;
      b.stage = 3;
      b.tier = 1;
      if (b.type == BuildingType::Dispenser) b.charges = 2;
      if (b.type == BuildingType::Graveyard) b.charges = 3;  // dug graves
      v.buildings.push_back(b);
      if (b.type == BuildingType::Graveyard) {
        Prop body;  // one poor soul awaiting burial beside the plot
        body.type = PropType::Body;
        body.variant = 1;
        body.pos = b.pos + glm::vec3(3.5f, 1.0f, 1.0f);
        body.asleep = false;
        body.radius = 0.6f;
        world.spawnProp(body);
      }
    }
    for (int k = 0; k < 3; ++k) {
      Prop s;
      s.type = PropType::Scaffold;
      s.resource = static_cast<float>(1 + k * 2);  // 1, 3, 5 stacks
      s.radius = 1.0f;
      s.pos = v.center + right * (static_cast<float>(k - 1) * 6.0f) - f * 14.0f;
      s.pos.y = world.terrain.heightAt(s.pos.x, s.pos.z) + 0.7f;
      s.asleep = true;
      world.spawnProp(s);
    }
    cam.focus = world.home().center + f * 12.0f;
    cam.distance = 60.0f;
    cam.yaw = 2.3f;
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
  for (const Village& vil : w.villages) {
  for (const Villager& v : vil.villagers) {
    addF(v.pos.x);
    addF(v.pos.y);
    addF(v.pos.z);
    addF(v.hunger);
    int s = static_cast<int>(v.state), j = static_cast<int>(v.job);
    int a = v.alive ? 1 : 0;
    h = fnvMix(h, &s, sizeof s);
    h = fnvMix(h, &j, sizeof j);
    h = fnvMix(h, &a, sizeof a);
  }
  int counters[4] = {vil.wood, vil.food, vil.population(), vil.owner};
  h = fnvMix(h, counters, sizeof counters);
  for (const Building& b : vil.buildings) {
    int info[4] = {static_cast<int>(b.type), b.stage, b.level, b.charges};
    h = fnvMix(h, info, sizeof info);
  }
  int fieldCount = static_cast<int>(vil.fields.size());
  h = fnvMix(h, &fieldCount, sizeof fieldCount);
  for (int g = 0; g < tune::kMaxGods; ++g) addF(vil.belief[g]);
  }
  for (int g = 0; g < tune::kMaxGods; ++g) {
    addF(w.gods[g].mana);
    // The AI hands are sim state too (villagers fear them).
    if (w.ai[g].handPos.y < 1.0e8f) {
      addF(w.ai[g].handPos.x);
      addF(w.ai[g].handPos.y);
      addF(w.ai[g].handPos.z);
    }
    int aiState[2] = {static_cast<int>(w.ai[g].phase),
                      static_cast<int>(w.ai[g].verb)};
    h = fnvMix(h, aiState, sizeof aiState);
  }
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
  Village& vil = world.home();

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

  // [3] Hand script: throw a villager - flail, stun, panic, recover; and the
  //     mortality tripwire, flipped as designed: maximum violence now kills.
  std::printf("[3] thrown villager\n");
  {
    Villager& v = vil.villagers[0];
    v.pos = vil.center + glm::vec3(0.0f, 5.0f, 0.0f);  // stun range, not lethal
    villagerReleased(world, 0, 0, glm::vec3(14.0f, 2.0f, 8.0f), false);
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
    check(v0.alive, "a stunning throw is survivable");
    // Maximum violence: the old invulnerability test, now asserting death.
    int deathsBefore = vil.deaths;
    vil.villagers[0].pos = vil.center + glm::vec3(0.0f, 60.0f, 0.0f);
    villagerReleased(world, 0, 0, glm::vec3(65.0f, 0.0f, 0.0f), false);
    for (int i = 0; i < 1200 && vil.deaths == deathsBefore; ++i) world.update(dt);
    check(!vil.villagers[0].alive, "a 65 m/s impact kills");
    int bodies = 0;
    for (const Prop& p : world.props)
      if (p.alive && p.type == PropType::Body) ++bodies;
    check(bodies >= 1, "a body remains");
  }

  // [4] Drop-to-assign resolution rules.
  std::printf("[4] drop-to-assign\n");
  {
    glm::vec3 fieldP(vil.fields[0].center.x, 0.0f, vil.fields[0].center.y);
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
    villagerReleased(world, 0, 5, glm::vec3(0.3f, 0.0f, 0.2f), true);
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
    check(world.gods[0].temple.founded, "temple founded");
    float distTV = glm::distance(glm::vec2(world.gods[0].temple.pos.x, world.gods[0].temple.pos.z),
                                 glm::vec2(vil.center.x, vil.center.z));
    std::printf("      temple at (%.0f, %.0f), %.0f m from the village | mana %.0f | belief %.2f\n",
                world.gods[0].temple.pos.x, world.gods[0].temple.pos.z, distTV, world.gods[0].mana,
                vil.belief[0]);
    check(distTV > 40.0f && distTV < 70.0f, "temple stands apart, near the village");
    check(world.gods[0].temple.pos.y > 1.0f, "temple on land");
    check(vil.resolveJobAtPoint(world, vil.buildings[vil.centerIdx].pos) ==
              Job::Worshipper,
          "totem -> worshipper");
    check(world.insideInfluence(vil.center), "village inside influence");
    check(world.insideInfluence(world.gods[0].temple.pos), "temple inside influence");

    glm::vec2 c2(vil.center.x, vil.center.z);
    glm::vec2 awayDir = glm::length(c2) > 1.0f ? -glm::normalize(c2)
                                               : glm::vec2(0.7071f, 0.7071f);
    glm::vec3 farPoint(awayDir.x * Terrain::SIZE * 0.45f, 0.0f,
                       awayDir.y * Terrain::SIZE * 0.45f);
    check(!world.insideInfluence(farPoint), "far shore outside influence");

    world.gods[0].mana = 5.0f;
    check(!world.castFoodMiracle(vil.center), "cast fails without mana");
    world.gods[0].mana = 100.0f;
    check(!world.castFoodMiracle(farPoint), "cast fails outside influence");

    float beliefBefore = vil.belief[0];
    auto countFood = [&]() {
      int n = 0;
      for (const Prop& p : world.props)
        if (p.alive && p.type == PropType::Food) ++n;
      return n;
    };
    int foodBefore = countFood();
    check(world.castFoodMiracle(vil.center + glm::vec3(5.0f, 0.0f, 5.0f)),
          "cast succeeds inside influence");
    check(world.gods[0].mana == 100.0f - tune::kFoodMiracleCost, "mana was spent");
    check(countFood() >= foodBefore + tune::kFoodMiracleBundles, "food rained from heaven");
    check(vil.belief[0] > beliefBefore, "witnesses believed harder");

    // A fresh world: does the starter worshipper alone fill the pool?
    World w3;
    w3.generate(seed);
    w3.dayCycle.secondsPerDay = 240.0f;
    float manaStart = w3.gods[0].mana;
    bool dancerSeen = false;
    for (int i = 0; i < static_cast<int>(240.0f / dt); ++i) {
      w3.update(dt);
      if ((i & 127) == 0 && w3.home().activeWorshippers() > 0) dancerSeen = true;
    }
    std::printf("      one day of worship: mana %.0f (from %.0f), belief %.2f\n",
                w3.gods[0].mana, manaStart, w3.home().belief[0]);
    check(dancerSeen, "the worshipper danced at the totem");
    check(w3.gods[0].mana > manaStart + 5.0f, "worship generated mana");
  }

  // [9] Scaffolds and the building roster.
  std::printf("[9] scaffolds & building\n");
  {
    World w4;
    w4.generate(seed);
    Village& v4 = w4.home();
    // Perpetual noon: this section tests construction logic, not the schedule.
    w4.dayCycle.t = 0.45f;
    w4.dayCycle.secondsPerDay = 1.0e6f;

    auto spawnScaffold = [&](glm::vec3 pos, int count) {
      Prop s;
      s.type = PropType::Scaffold;
      s.resource = static_cast<float>(count);
      s.radius = 0.9f + 0.18f * static_cast<float>(count);
      s.pos = pos;
      s.pos.y = w4.terrain.heightAt(pos.x, pos.z) + 0.7f;
      s.asleep = true;
      return w4.spawnProp(s);
    };
    auto findSpot = [&](int count) {
      for (float r = 22.0f; r < tune::kBuildPlacementRange; r += 4.0f)
        for (float a = 0.0f; a < 6.28f; a += 0.3f) {
          glm::vec3 p = v4.center + glm::vec3(std::sin(a) * r, 0.0f, std::cos(a) * r);
          p.y = w4.terrain.heightAt(p.x, p.z);
          if (w4.scaffoldPlacementValid(p, count)) return p;
        }
      return v4.center;  // will fail validity; the check will catch it
    };

    // a) End-to-end: the builder crafts a scaffold once the starter site is
    //    done, and a placed scaffold gets built into a house.
    v4.wood = 20;
    int craftedIdx = -1;
    for (int i = 0; i < 18000 && craftedIdx < 0; ++i) {  // up to 300 s
      w4.update(dt);
      for (std::size_t p = 0; p < w4.props.size(); ++p)
        if (w4.props[p].alive && w4.props[p].type == PropType::Scaffold)
          craftedIdx = static_cast<int>(p);
    }
    check(craftedIdx >= 0, "the workshop crafted a scaffold");
    check(v4.scaffoldsCrafted > 0, "crafting was counted (and paid in wood)");
    if (craftedIdx >= 0) {
      int capBefore = v4.housingCapacity();
      glm::vec3 spot = findSpot(1);
      w4.props[craftedIdx].pos = spot + glm::vec3(0, 0.7f, 0);
      check(w4.tryPlaceScaffold(craftedIdx, BuildingType::Store),
            "crafted scaffold placed as a site");
      bool built = false;
      for (int i = 0; i < 18000 && !built; ++i) {
        w4.update(dt);
        built = v4.housingCapacity() > capBefore;
      }
      check(built, "builders raised the small abode from the scaffold");
    }

    // b) Combining stacks.
    int a1 = spawnScaffold(v4.center + glm::vec3(20, 0, 4), 1);
    int a2 = spawnScaffold(v4.center + glm::vec3(20.8f, 0, 4), 1);
    int merged = w4.tryCombineScaffold(a1);
    check(merged == a2 && std::lround(w4.props[a2].resource) == 2, "1 + 1 = a 2-stack");
    int b5 = spawnScaffold(v4.center + glm::vec3(24, 0, 8), 5);
    int b3 = spawnScaffold(v4.center + glm::vec3(24.7f, 0, 8), 3);
    check(w4.tryCombineScaffold(b3) < 0, "5 + 3 refused (cap is 7)");
    (void)b5;

    // c) The full roster: place each stack size and complete it directly.
    struct Placement {
      int count;
      BuildingType civic;
      BuildingType expect;
    };
    const Placement kRoster[] = {
        {2, BuildingType::Store, BuildingType::LargeAbode},
        {3, BuildingType::Store, BuildingType::Store},
        {3, BuildingType::Creche, BuildingType::Creche},
        {3, BuildingType::Graveyard, BuildingType::Graveyard},
        {4, BuildingType::Store, BuildingType::FieldSite},
        {6, BuildingType::Store, BuildingType::Dispenser},
        {7, BuildingType::Store, BuildingType::Wonder},
    };
    int foodCapBefore = v4.foodCap();
    std::size_t fieldsBefore = v4.fields.size();
    bool rosterOk = true;
    for (const Placement& pl : kRoster) {
      glm::vec3 spot = findSpot(pl.count);
      int idx = spawnScaffold(spot, pl.count);
      if (!w4.tryPlaceScaffold(idx, pl.civic)) {
        rosterOk = false;
        std::printf("      FAILED to place %d-stack\n", pl.count);
        continue;
      }
      int newIdx = static_cast<int>(v4.buildings.size()) - 1;
      rosterOk &= v4.buildings[newIdx].type == pl.expect;
      v4.onBuildingComplete(w4, newIdx);
    }
    check(rosterOk, "every stack size placed and mapped to its building");
    check(v4.foodCap() == foodCapBefore + tune::kStoreFoodCap, "the Store raised the caps");
    check(v4.fields.size() == fieldsBefore + 1, "the Field planted a new plot");

    // d) Center upgrade: a 5-stack at the totem.
    float ringBefore = v4.influenceRadius();
    int c5 = spawnScaffold(v4.buildings[v4.centerIdx].pos + glm::vec3(1, 0, 1), 5);
    check(w4.tryPlaceScaffold(c5, BuildingType::Store), "5-stack at the totem accepted");
    check(v4.buildings[v4.centerIdx].level == 2, "the Center leveled up");
    check(v4.influenceRadius() > ringBefore, "influence grew with the Center");

    // e) Dispenser: free casting from charges.
    int dispIdx = -1;
    for (std::size_t b = 0; b < v4.buildings.size(); ++b)
      if (v4.buildings[b].type == BuildingType::Dispenser) dispIdx = static_cast<int>(b);
    check(dispIdx >= 0, "a dispenser stands");
    if (dispIdx >= 0) {
      v4.buildings[dispIdx].charges = 2;
      w4.gods[0].mana = 0.0f;
      check(w4.castFoodMiracle(v4.buildings[dispIdx].pos + glm::vec3(3, 0, 0)),
            "cast from dispenser charges with an empty pool");
      check(v4.buildings[dispIdx].charges == 1, "a charge was spent");
    }

    // f) Wonder aura.
    int wonderIdx = -1;
    for (std::size_t b = 0; b < v4.buildings.size(); ++b)
      if (v4.buildings[b].type == BuildingType::Wonder) wonderIdx = static_cast<int>(b);
    check(wonderIdx >= 0 && v4.insideWonderAura(v4.buildings[wonderIdx].pos),
          "the Wonder's aura is live");

    // g) Storage caps pause absorption.
    v4.food = v4.foodCap();
    Prop overflow;
    overflow.type = PropType::Food;
    overflow.resource = 3.0f;
    overflow.radius = 0.45f;
    overflow.pos = v4.storagePos() + glm::vec3(0, 2, 0);
    overflow.asleep = false;
    int ovIdx = w4.spawnProp(overflow);
    for (int i = 0; i < 300; ++i) w4.update(dt);
    check(v4.food == v4.foodCap() && w4.props[ovIdx].alive,
          "a full store refuses the deposit (the bundle stays)");

    // h) Invalid placements refused.
    glm::vec3 deep(v4.center.x, 0.0f, v4.center.z);
    deep += glm::vec3(200.0f, 0.0f, 200.0f);
    check(!w4.scaffoldPlacementValid(deep, 1), "placement far outside refused");
    check(!w4.scaffoldPlacementValid(w4.gods[0].temple.pos, 1),
          "placement on the temple refused");
  }

  // [10] Mortality & burial: the Graveyard earns its headstones.
  std::printf("[10] mortality & burial\n");
  {
    // -- impact death, then the village buries its dead --
    World w5;
    w5.generate(seed);
    w5.dayCycle.t = 0.45f;
    w5.dayCycle.secondsPerDay = 1.0e6f;  // frozen noon isolates the mechanics
    Village& v5 = w5.home();

    auto spawnStack = [&](World& w, glm::vec3 pos, int count) {
      Prop s;
      s.type = PropType::Scaffold;
      s.resource = static_cast<float>(count);
      s.radius = 1.0f;
      s.pos = pos;
      s.pos.y = w.terrain.heightAt(pos.x, pos.z) + 0.7f;
      s.asleep = true;
      return w.spawnProp(s);
    };
    auto buildAt = [&](World& w, int count, BuildingType civic) {
      for (float r = 22.0f; r < tune::kBuildPlacementRange; r += 4.0f)
        for (float a = 0.0f; a < 6.28f; a += 0.3f) {
          glm::vec3 p = w.home().center +
                        glm::vec3(std::sin(a) * r, 0.0f, std::cos(a) * r);
          p.y = w.terrain.heightAt(p.x, p.z);
          if (w.scaffoldPlacementValid(p, count)) {
            int idx = spawnStack(w, p, count);
            if (w.tryPlaceScaffold(idx, civic)) {
              int nb = static_cast<int>(w.home().buildings.size()) - 1;
              w.home().onBuildingComplete(w, nb);
              return nb;
            }
          }
        }
      return -1;
    };
    int graveyardIdx = buildAt(w5, 3, BuildingType::Graveyard);
    check(graveyardIdx >= 0, "a graveyard stands");

    int popBefore = v5.population();
    v5.villagers[5].pos = v5.center + glm::vec3(6.0f, 40.0f, 6.0f);
    villagerReleased(w5, 0, 5, glm::vec3(10.0f, -20.0f, 5.0f), false);
    for (int i = 0; i < 1200 && v5.deaths == 0; ++i) w5.update(dt);
    check(v5.deaths == 1, "a hard fall killed");
    check(!v5.villagers[5].alive, "the villager is gone");
    check(v5.population() == popBefore - 1, "population fell");
    auto countBodies = [](const World& w) {
      int n = 0;
      for (const Prop& p : w.props)
        if (p.alive && p.type == PropType::Body) ++n;
      return n;
    };
    check(countBodies(w5) == 1, "a body lies in the world");

    bool buried = false;
    for (int i = 0; i < 36000 && !buried; ++i) {
      w5.update(dt);
      buried = v5.burials > 0;
    }
    check(buried, "the village buried its dead");
    check(v5.buildings[graveyardIdx].charges == 1, "a grave was dug");
    check(countBodies(w5) == 0, "the body was laid to rest");

    // -- drowning: thrown far out to sea, too far to swim home --
    World w6;
    w6.generate(seed);
    w6.dayCycle.t = 0.45f;
    w6.dayCycle.secondsPerDay = 1.0e6f;
    Village& v6 = w6.home();
    glm::vec3 spot = v6.fishingSpots.empty() ? v6.center : v6.fishingSpots[0];
    glm::vec3 out = glm::normalize(
        glm::vec3(spot.x - v6.center.x, 0.0f, spot.z - v6.center.z) +
        glm::vec3(0.001f, 0.0f, 0.0f));
    glm::vec3 deepPoint = spot;
    for (float d = 4.0f; d < 160.0f; d += 4.0f) {
      glm::vec3 p = spot + out * d;
      if (w6.terrain.heightAt(p.x, p.z) < -6.0f) {
        deepPoint = p + out * 30.0f;  // beyond a 16 s swim home
        break;
      }
    }
    v6.villagers[6].pos = deepPoint + glm::vec3(0.0f, 3.0f, 0.0f);
    villagerReleased(w6, 0, 6, glm::vec3(0.0f), false);
    for (int i = 0; i < 2400 && v6.deaths == 0; ++i) w6.update(dt);
    check(v6.deaths == 1 && !v6.villagers[6].alive, "too far from shore: drowned");
    bool bodyFloats = false;
    for (const Prop& p : w6.props)
      if (p.alive && p.type == PropType::Body && p.pos.y > -2.0f) bodyFloats = true;
    check(bodyFloats, "the body floats");

    // -- starvation: an empty larder eventually kills --
    World w7;
    w7.generate(seed);
    w7.dayCycle.secondsPerDay = 60.0f;
    for (int i = 0; i < 16000 && w7.home().deaths == 0; ++i) {
      w7.home().food = 0;  // an enforced famine
      w7.update(dt);
    }
    check(w7.home().deaths >= 1, "famine starves");
    check(countBodies(w7) >= 1, "starvation leaves a body");
  }

  // [11] Many villages, one god.
  std::printf("[11] many villages\n");
  {
    std::printf("      %zu villages founded\n", world.villages.size());
    check(world.villages.size() >= 2, "the island hosts multiple villages");
    bool sepOk = true, neutralOk = true, noWorship = true;
    for (std::size_t a = 0; a < world.villages.size(); ++a) {
      for (std::size_t b = a + 1; b < world.villages.size(); ++b)
        sepOk &= glm::distance(glm::vec2(world.villages[a].center.x,
                                         world.villages[a].center.z),
                               glm::vec2(world.villages[b].center.x,
                                         world.villages[b].center.z)) >
                 tune::kVillageMinSeparation - 1.0f;
      if (a > 0) {
        neutralOk &= world.villages[a].owner == -1;
        for (const Villager& v : world.villages[a].villagers)
          noWorship &= v.job != Job::Worshipper;
      }
    }
    check(sepOk, "villages keep their distance");
    check(neutralOk, "the others start neutral");
    check(noWorship, "neutral villagers don't worship you");
    check(world.villages[0].owner == 0, "the home village is yours");
    if (world.villages.size() >= 2) {
      const Village& n = world.villages[1];
      check(!world.insideInfluence(n.center),
            "a neutral village sits outside your influence");
      float nb = n.belief[0], pb = world.home().belief[0];
      world.notifyDivineEvent(0, n.center, 0.0f, 0.2f);
      check(world.villages[1].belief[0] > nb,
            "witnesses at the neutral village believed");
      check(world.home().belief[0] == pb, "your own village saw nothing");
    }
  }

  // [12] Gods & conversion: the ratchet.
  std::printf("[12] gods & conversion\n");
  {
    World w8;
    w8.generate(seed);
    const float dt12 = 1.0f / 60.0f;
    check(w8.gods[0].active && w8.gods[0].isPlayer, "the player god reigns");
    check(!w8.gods[1].active, "no rival is active yet");
    check(w8.villages.size() >= 2, "a neutral village waits to be courted");

    // Court the nearest neutral with repeated impressive acts.
    Village& n = w8.villages[1];
    int guard = 0;
    while (n.owner != 0 && guard++ < 200) {
      w8.notifyDivineEvent(0, n.center, 0.0f, 0.15f);
      for (int s = 0; s < 30; ++s) w8.update(dt12);
    }
    std::printf("      converted after %d offerings | belief %.2f\n", guard,
                n.belief[0]);
    check(n.owner == 0, "the neutral village converted to you");
    check(w8.insideInfluence(n.center, 0), "its influence ring now answers to you");
    check(n.belief[0] > 0.4f, "its faith stands high");

    // Their worship now fills YOUR pool.
    int devotee = -1;
    for (std::size_t j = 0; j < n.villagers.size(); ++j)
      if (n.villagers[j].alive && n.villagers[j].job == Job::None &&
          n.villagers[j].scale > 0.9f)
        devotee = static_cast<int>(j);
    check(devotee >= 0, "an idle adult can be devoted");
    if (devotee >= 0) {
      n.villagers[devotee].job = Job::Worshipper;
      w8.dayCycle.t = 0.40f;
      w8.dayCycle.secondsPerDay = 240.0f;
      w8.gods[0].mana = 10.0f;
      for (int s = 0; s < static_cast<int>(60.0f / dt12); ++s) w8.update(dt12);
      std::printf("      converted village's worship: mana %.1f\n", w8.gods[0].mana);
      check(w8.gods[0].mana > 10.0f, "their worship fills your pool");
    }

    // The ratchet: an owned village resists overwhelming faith alone...
    w8.gods[1].active = true;  // a rival stirs (M5 embodies it)
    Village& h = w8.home();
    h.belief[1] = tune::kStealBelief + 0.05f;
    h.belief[0] = tune::kStealOwnerBelow + 0.10f;
    w8.update(dt12);
    check(h.owner == 0, "an owned village resists while its owner holds");
    // ...and falls only when the owner has lapsed.
    h.belief[0] = tune::kStealOwnerBelow - 0.05f;
    w8.update(dt12);
    check(h.owner == 1, "a lapsed village falls to overwhelming faith");

    // Neutrals need a clear lead, not just a majority.
    if (w8.villages.size() >= 3) {
      Village& m = w8.villages[2];
      m.belief[0] = 0.55f;
      m.belief[1] = 0.50f;
      w8.update(dt12);
      check(m.owner == -1, "a contested neutral stays neutral");
      m.belief[1] = 0.30f;
      w8.update(dt12);
      check(m.owner == 0, "a clear lead converts");
    }

    // Gift attribution: a bundle hurled onto their pile is credited to you.
    World w9;
    w9.generate(seed);
    Village& n2 = w9.villages[1];
    float nb = n2.belief[0];
    Prop gift;
    gift.type = PropType::Food;
    gift.resource = 3.0f;
    gift.radius = 0.45f;
    gift.thrownByGod = 0;
    gift.pos = n2.storagePos() + glm::vec3(0.0f, 3.0f, 0.0f);
    gift.asleep = false;
    w9.spawnProp(gift);
    for (int s = 0; s < 300; ++s) w9.update(dt12);
    check(w9.villages[1].belief[0] > nb,
          "a gift landed on their pile is credited to you");
  }

  // [13] The rival god: symmetric skirmish start, an embodied AI hand that
  // plays through the player's verbs, and a game that can be lost.
  std::printf("[13] the rival god\n");
  {
    const float dtr = 1.0f / 60.0f;
    World w10;
    w10.generate(seed, 2);
    check(w10.gods[1].active && !w10.gods[1].isPlayer && w10.gods[1].ai,
          "a rival god wakes on skirmish worlds");
    int rivalHome = -1, rivalCount = 0;
    for (std::size_t v = 0; v < w10.villages.size(); ++v)
      if (w10.villages[v].owner == 1) {
        rivalHome = static_cast<int>(v);
        ++rivalCount;
      }
    check(rivalHome > 0 && rivalCount == 1, "the rival owns exactly one village");
    check(w10.villages[0].owner == 0, "your home is still yours");
    if (rivalHome > 0) {
      const Village& rh = w10.villages[rivalHome];
      float dHomes = glm::distance(glm::vec2(rh.center.x, rh.center.z),
                                   glm::vec2(w10.home().center.x, w10.home().center.z));
      float dTemple = w10.gods[1].temple.founded
                          ? glm::distance(glm::vec2(w10.gods[1].temple.pos.x,
                                                    w10.gods[1].temple.pos.z),
                                          glm::vec2(rh.center.x, rh.center.z))
                          : -1.0f;
      std::printf("      rival home %.0f m from yours | its temple %.0f m out\n",
                  dHomes, dTemple);
      check(dHomes > tune::kVillageMinSeparation - 1.0f,
            "the rival settles far from you");
      check(w10.gods[1].temple.founded, "the rival founded its own temple");
      check(dTemple > 30.0f && dTemple < 80.0f, "its temple stands apart, nearby");
      bool hasWorship = false;
      for (const Villager& p : rh.villagers) hasWorship |= p.job == Job::Worshipper;
      check(hasWorship, "its village worships it from the start");
      check(rh.belief[1] >= tune::kBeliefStart - 0.01f, "its faith is seeded");
      check(w10.insideInfluence(rh.center, 1), "its ring answers to it");
      check(!w10.insideInfluence(rh.center, 0), "and not to you");
    }

    // Its worship fills ITS pool.
    w10.dayCycle.t = 0.40f;
    w10.dayCycle.secondsPerDay = 240.0f;
    float rivalMana = w10.gods[1].mana;
    for (int s = 0; s < 30 * 60; ++s) w10.update(dtr);
    std::printf("      rival mana %.1f -> %.1f\n", rivalMana, w10.gods[1].mana);
    check(w10.gods[1].mana > rivalMana, "rival worship fills the rival pool");

    // The embodied hand devotes: strip its worshippers and watch it restore
    // the dance - grab an idle adult, carry it, drop it on the totem.
    if (rivalHome > 0) {
      Village& rh = w10.villages[rivalHome];
      for (Villager& p : rh.villagers)
        if (p.alive && p.job == Job::Worshipper) {
          p.job = Job::None;
          p.state = VState::Idle;
          p.stateTimer = 0.2f;
        }
      int guard = 0;
      bool restored = false;
      for (; guard < 90 * 60 && !restored; ++guard) {
        w10.update(dtr);
        for (const Villager& p : rh.villagers)
          restored |= p.alive && p.job == Job::Worshipper;
      }
      std::printf("      devotion restored after %.1f s (%d by hand so far)\n",
                  static_cast<float>(guard) * dtr, w10.ai[1].devotions);
      check(restored, "the AI devotes an idle adult by hand");

      // Feeding: an empty larder with mana banked brings a food miracle.
      rh.food = 0;
      w10.gods[1].mana = 100.0f;
      int feedsBefore = w10.ai[1].feeds;
      for (int s = 0; s < 60 * 60 && w10.ai[1].feeds == feedsBefore; ++s)
        w10.update(dtr);
      check(w10.ai[1].feeds > feedsBefore, "a hungry village gets a miracle");
      check(w10.gods[1].mana < 100.0f, "and the rival paid mana for it");

      // Courting: quiet the governor (larder full, dancers content), plant a
      // gift inside the rival's temple ring but beyond its villagers' reach,
      // and watch the hand hurl it onto the nearest neutral's pad. Deliveries
      // only count when witnessed, so run this in daylight - gifts landing on
      // a sleeping village convince nobody.
      rh.food = 40;
      w10.dayCycle.t = 0.35f;
      int neutralIdx = -1;
      for (std::size_t v = 0; v < w10.villages.size(); ++v)
        if (w10.villages[v].founded && w10.villages[v].owner < 0 &&
            w10.villages[v].population() > 0)
          neutralIdx = static_cast<int>(v);
      if (neutralIdx >= 0) {
        glm::vec2 t2(w10.gods[1].temple.pos.x, w10.gods[1].temple.pos.z);
        glm::vec2 c2(rh.center.x, rh.center.z);
        glm::vec2 dir = glm::normalize(t2 - c2);
        glm::vec2 spot2 = t2 + dir * 55.0f;  // far side of the temple ring
        Prop bait;
        bait.type = PropType::Food;
        bait.resource = 3.0f;
        bait.radius = 0.45f;
        bait.pos = glm::vec3(spot2.x,
                             w10.terrain.heightAt(spot2.x, spot2.y) + 0.4f,
                             spot2.y);
        bait.asleep = true;
        w10.spawnProp(bait);
        float nBelief = w10.villages[neutralIdx].belief[1];
        int giftsBefore = w10.ai[1].gifts;
        // Decay pulls belief down every frame; only a credited delivery can
        // push it UP. Any upward step proves the gift landed and was seen.
        bool credited = false;
        float prevB = nBelief;
        int guard2 = 0;
        for (; guard2 < 90 * 60 && !credited; ++guard2) {
          w10.update(dtr);
          float b = w10.villages[neutralIdx].belief[1];
          credited |= b > prevB + 1.0e-6f;
          prevB = b;
        }
        std::printf(
            "      gift run: %d -> %d gifts | neutral belief in rival %.3f -> %.3f "
            "after %.0f s\n",
            giftsBefore, w10.ai[1].gifts, nBelief,
            w10.villages[neutralIdx].belief[1],
            static_cast<float>(guard2) * dtr);
        check(w10.ai[1].gifts > giftsBefore, "the hand ran a gift to the neutral");
        check(credited, "the delivery won the rival some faith there");
      }
    }

    // A skirmish can be LOST: the ratchet takes your last village and breaks
    // you; the rival can be broken the same way, and its hand goes still.
    World w11;
    w11.generate(seed, 2);
    check(!w11.godBroken(0) && !w11.godBroken(1), "both gods start standing");
    Village& ph = w11.villages[0];
    ph.belief[1] = tune::kStealBelief + 0.05f;
    ph.belief[0] = tune::kStealOwnerBelow - 0.05f;
    w11.update(dtr);
    check(ph.owner == 1, "your lapsed home falls to overwhelming rival faith");
    check(w11.godBroken(0), "with no villages left, you are broken");
    int rivalHome2 = -1;
    for (std::size_t v = 0; v < w11.villages.size(); ++v)
      if (w11.villages[v].owner == 1 && static_cast<int>(v) != 0)
        rivalHome2 = static_cast<int>(v);
    if (rivalHome2 > 0) {
      // Hand every rival village to the player: the rival breaks too.
      for (Village& v : w11.villages)
        if (v.founded && v.owner == 1) {
          v.belief[0] = tune::kStealBelief + 0.05f;
          v.belief[1] = tune::kStealOwnerBelow - 0.05f;
        }
      w11.update(dtr);
      check(w11.godBroken(1), "the rival can be broken the same way");
      const GodAI& brain = w11.ai[1];
      int actsBefore = brain.devotions + brain.feeds + brain.placements +
                       brain.combines + brain.gifts + brain.courtCasts;
      for (int s = 0; s < 20 * 60; ++s) w11.update(dtr);
      int actsAfter = brain.devotions + brain.feeds + brain.placements +
                      brain.combines + brain.gifts + brain.courtCasts;
      check(actsAfter == actsBefore && brain.phase == GodAI::Phase::Rest,
            "a broken god's hand goes still");
    }

    // AI-vs-AI: both gods played by brains, twice, bit-for-bit identical.
    World wa, wb;
    wa.generate(seed, 2);
    wb.generate(seed, 2);
    wa.gods[0].ai = true;
    wb.gods[0].ai = true;
    wa.dayCycle.t = wb.dayCycle.t = 0.40f;
    wa.dayCycle.secondsPerDay = wb.dayCycle.secondsPerDay = 240.0f;
    for (int s = 0; s < 60 * 60; ++s) {
      wa.update(dtr);
      wb.update(dtr);
    }
    std::uint64_t ha = worldChecksum(wa), hb = worldChecksum(wb);
    int warActs = 0;
    for (int g = 0; g < tune::kMaxGods; ++g)
      warActs += wa.ai[g].devotions + wa.ai[g].feeds + wa.ai[g].placements +
                 wa.ai[g].combines + wa.ai[g].gifts + wa.ai[g].courtCasts;
    std::printf("      60 s of war: %d divine acts | checksum %016llx\n", warActs,
                static_cast<unsigned long long>(ha));
    check(ha == hb, "AI-vs-AI matches are deterministic");
    check(warActs > 0, "the gods actually played");
  }

  // [6] Three-day economy & schedule soak. Days are shrunk to 240 s - short
  // enough to simulate fast, long enough that walking/chopping (real-time
  // actions) still fit inside a work day.
  std::printf("[6] three-day soak\n");
  {
    World w2;
    w2.generate(seed);
    w2.dayCycle.secondsPerDay = 240.0f;
    Village& v2 = w2.home();
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
        "houses %d | scaffolds %d | deaths %d | stuck %d | midnight asleep %.0f%% | "
        "noon active %.0f%% | mana %.0f | belief %.2f\n",
        v2.wood, v2.woodProduced, v2.food, v2.foodProduced, v2.mealsEaten,
        v2.population(), stage3Houses, v2.scaffoldsCrafted, v2.deaths, v2.stuckEvents,
        midnightSleep * 100.0f, noonActive * 100.0f, w2.gods[0].mana, v2.belief[0]);
    int totalDeaths = 0;
    bool allProduced = true, allAte = true;
    for (const Village& v : w2.villages) {
      totalDeaths += v.deaths;
      allProduced &= v.foodProduced > 0;
      allAte &= v.mealsEaten > 0;
    }
    check(totalDeaths == 0, "no village loses anybody in health");
    check(allProduced, "every village (neutral too) produced food");
    check(allAte, "every village ate");
    check(w2.gods[0].mana > tune::kManaStart, "worship filled the mana pool");
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

// AI-vs-AI skirmish, no window: both gods played by brains at 240 s days.
// The balance tool - watch the war unfold day by day, deterministically.
int runMatch(std::uint32_t seed, int days) {
  World w;
  w.generate(seed, 2);
  if (!w.gods[1].active) {
    std::printf("Island %u is too hostile for a rival (one village site).\n", seed);
    return 1;
  }
  w.gods[0].ai = true;
  w.dayCycle.secondsPerDay = 240.0f;
  const float dt = 1.0f / 60.0f;
  const int stepsPerDay = static_cast<int>(240.0f / dt);

  std::printf("AI-vs-AI match | seed %u | %zu villages\n", seed, w.villages.size());
  for (int day = 1; day <= days; ++day) {
    for (int s = 0; s < stepsPerDay; ++s) w.update(dt);
    int owned[2] = {0, 0}, neutral = 0, pop = 0;
    for (const Village& v : w.villages) {
      if (!v.founded) continue;
      pop += v.population();
      if (v.owner == 0) ++owned[0];
      else if (v.owner == 1) ++owned[1];
      else ++neutral;
    }
    int acts[2] = {0, 0};
    for (int g = 0; g < 2; ++g)
      acts[g] = w.ai[g].devotions + w.ai[g].feeds + w.ai[g].placements +
                w.ai[g].combines + w.ai[g].gifts + w.ai[g].courtCasts;
    std::printf(
        "day %2d | gold %d villages, mana %3.0f, %3d acts | crimson %d villages, "
        "mana %3.0f, %3d acts | neutral %d | pop %d\n",
        day, owned[0], w.gods[0].mana, acts[0], owned[1], w.gods[1].mana, acts[1],
        neutral, pop);
    for (std::size_t v = 0; v < w.villages.size(); ++v) {
      const Village& vil = w.villages[v];
      if (!vil.founded) continue;
      std::printf("        village %zu: owner %2d | belief gold %.2f crimson %.2f "
                  "| pop %d | food %d\n",
                  v, vil.owner, vil.belief[0], vil.belief[1], vil.population(),
                  vil.food);
    }
    if (w.godBroken(0) || w.godBroken(1)) {
      std::printf("%s\n", w.godBroken(0) ? "The crimson god has won."
                                         : "The gold god has won.");
      break;
    }
  }
  std::printf("checksum %016llx\n",
              static_cast<unsigned long long>(worldChecksum(w)));
  return 0;
}

int main(int argc, char** argv) {
  std::uint32_t seed = 20260702u;
  bool headless = false;
  bool rival = true;
  bool match = false;
  int matchDays = 10;
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
    } else if (arg == "--no-rival") {
      rival = false;
    } else if (arg == "--match") {
      match = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') matchDays = std::atoi(argv[++i]);
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
  if (match) return runMatch(seed, matchDays);

  App app;
  app.seed = seed;
  app.rivalEnabled = rival;
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
