#include "Models.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace models {

namespace {

glm::mat4 at(float x, float y, float z) {
  return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
}

glm::mat4 atRotY(float x, float y, float z, float yaw) {
  return glm::rotate(at(x, y, z), yaw, glm::vec3(0, 1, 0));
}

const glm::vec3 kWood(0.45f, 0.32f, 0.20f);
const glm::vec3 kWoodLight(0.58f, 0.44f, 0.28f);
const glm::vec3 kThatch(0.62f, 0.52f, 0.28f);
const glm::vec3 kPlaster(0.80f, 0.72f, 0.58f);
const glm::vec3 kStone(0.48f, 0.46f, 0.44f);

}  // namespace

// ------------------------------------------------------------- villager body

MeshData villagerHead(int variant) {
  static const glm::vec3 skins[3] = {
      {0.85f, 0.66f, 0.50f}, {0.72f, 0.52f, 0.38f}, {0.90f, 0.72f, 0.58f}};
  static const glm::vec3 hairs[3] = {
      {0.25f, 0.18f, 0.10f}, {0.10f, 0.09f, 0.08f}, {0.55f, 0.42f, 0.20f}};
  MeshData md;
  md.addBox(glm::mat4(1.0f), {0.14f, 0.155f, 0.13f}, skins[variant % 3]);
  md.addBox(at(0.0f, 0.13f, -0.015f), {0.15f, 0.05f, 0.14f}, hairs[variant % 3]);
  return md;
}

MeshData villagerTorso() {
  MeshData md;
  // Near-white tunic: the job color multiplies in via uTint.
  md.addBox(glm::mat4(1.0f), {0.24f, 0.33f, 0.145f}, glm::vec3(0.82f, 0.80f, 0.76f));
  return md;
}

MeshData villagerArm() {
  MeshData md;  // pivot at the shoulder, extends down
  md.addBox(at(0.0f, -0.28f, 0.0f), {0.075f, 0.29f, 0.08f}, glm::vec3(0.80f, 0.72f, 0.62f));
  return md;
}

MeshData villagerLeg() {
  MeshData md;  // pivot at the hip
  md.addBox(at(0.0f, -0.345f, 0.0f), {0.09f, 0.35f, 0.10f}, glm::vec3(0.34f, 0.28f, 0.22f));
  return md;
}

// ------------------------------------------------------------------ buildings

MeshData temple() {
  MeshData md;
  const glm::vec3 stoneLight(0.72f, 0.70f, 0.66f);
  const glm::vec3 stoneDark(0.55f, 0.53f, 0.50f);
  // Three-step platform.
  md.addBox(at(0.0f, 0.35f, 0.0f), {5.2f, 0.35f, 5.2f}, stoneDark);
  md.addBox(at(0.0f, 0.95f, 0.0f), {4.2f, 0.25f, 4.2f}, stoneLight);
  md.addBox(at(0.0f, 1.40f, 0.0f), {3.3f, 0.20f, 3.3f}, stoneDark);
  // Steps down the front (+z).
  md.addBox(at(0.0f, 0.35f, 5.9f), {1.6f, 0.35f, 0.7f}, stoneLight);
  md.addBox(at(0.0f, 0.90f, 4.9f), {1.4f, 0.28f, 0.7f}, stoneDark);
  // Columns at the corners of the top platform.
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2)
      md.addCylinder(at(sx * 2.5f, 3.4f, sz * 2.5f), 0.38f, 0.34f, 3.6f, 8, stoneLight);
  // Roof slabs.
  md.addBox(at(0.0f, 5.35f, 0.0f), {3.2f, 0.18f, 3.2f}, stoneDark);
  md.addBox(at(0.0f, 5.75f, 0.0f), {2.4f, 0.16f, 2.4f}, stoneLight);
  return md;
}

MeshData templeCrystal() {
  MeshData md;  // a gold obelisk floating in the sanctum
  const glm::vec3 gold(1.0f, 0.85f, 0.40f);
  md.addCylinder(at(0.0f, 2.9f, 0.0f), 0.55f, 0.0f, 1.7f, 6, gold);
  glm::mat4 flip = at(0.0f, 1.75f, 0.0f);
  flip = glm::rotate(flip, 3.14159f, glm::vec3(1, 0, 0));
  md.addCylinder(flip, 0.55f, 0.0f, 1.0f, 6, gold * 0.85f);
  return md;
}

MeshData houseStage(int stage) {
  MeshData md;
  const float hw = 2.3f, hd = 1.9f;  // half extents
  // Corner posts (all stages).
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2)
      md.addBox(at(sx * hw, 1.1f, sz * hd), {0.14f, 1.1f, 0.14f}, kWood);
  if (stage <= 0) {
    // Peg outline on the ground.
    md.addBox(at(0.0f, 0.12f, 0.0f), {hw, 0.06f, hd}, kWoodLight);
    return md;
  }
  float wallH = stage == 1 ? 0.9f : 1.9f;
  // Walls (front wall gets a door gap at stage >= 2 via two segments).
  md.addBox(at(0.0f, wallH * 0.5f + 0.1f, -hd), {hw, wallH * 0.5f, 0.10f}, kPlaster);
  md.addBox(at(-hw, wallH * 0.5f + 0.1f, 0.0f), {0.10f, wallH * 0.5f, hd}, kPlaster);
  md.addBox(at(hw, wallH * 0.5f + 0.1f, 0.0f), {0.10f, wallH * 0.5f, hd}, kPlaster);
  if (stage == 1) {
    md.addBox(at(0.0f, wallH * 0.5f + 0.1f, hd), {hw, wallH * 0.5f, 0.10f}, kPlaster);
  } else {
    // Front wall with a door opening.
    md.addBox(at(-hw * 0.62f, wallH * 0.5f + 0.1f, hd), {hw * 0.38f, wallH * 0.5f, 0.10f}, kPlaster);
    md.addBox(at(hw * 0.62f, wallH * 0.5f + 0.1f, hd), {hw * 0.38f, wallH * 0.5f, 0.10f}, kPlaster);
    md.addBox(at(0.0f, wallH * 0.80f + 0.1f, hd), {hw * 0.24f, wallH * 0.20f, 0.10f}, kPlaster);
    // Dark doorway inset.
    md.addBox(at(0.0f, wallH * 0.30f + 0.1f, hd - 0.04f), {hw * 0.20f, wallH * 0.30f, 0.05f},
              glm::vec3(0.12f, 0.09f, 0.06f));
  }
  if (stage >= 3) {
    // Thatched roof: two rotated slabs meeting at a ridge.
    const float roofH = 1.15f, roofLen = hd + 0.7f;
    glm::mat4 l = at(-hw * 0.52f, 2.0f + roofH * 0.5f, 0.0f);
    l = glm::rotate(l, 0.72f, glm::vec3(0, 0, 1));
    md.addBox(l, {hw * 0.78f, 0.09f, roofLen}, kThatch);
    glm::mat4 r = at(hw * 0.52f, 2.0f + roofH * 0.5f, 0.0f);
    r = glm::rotate(r, -0.72f, glm::vec3(0, 0, 1));
    md.addBox(r, {hw * 0.78f, 0.09f, roofLen}, kThatch);
    md.addBox(at(0.0f, 2.62f, 0.0f), {0.18f, 0.10f, roofLen * 0.92f}, kWood);
  }
  return md;
}

MeshData houseWindows() {
  MeshData md;
  const glm::vec3 glow(1.0f, 0.82f, 0.45f);
  md.addBox(at(-2.31f, 1.25f, 0.6f), {0.03f, 0.28f, 0.34f}, glow);
  md.addBox(at(2.31f, 1.25f, -0.6f), {0.03f, 0.28f, 0.34f}, glow);
  return md;
}

MeshData totem() {
  MeshData md;
  md.addCylinder(at(0.0f, 0.25f, 0.0f), 1.5f, 1.3f, 0.5f, 9, kStone);
  md.addCylinder(at(0.0f, 1.6f, 0.0f), 0.5f, 0.42f, 2.4f, 8, kWood);
  md.addCylinder(at(0.0f, 3.1f, 0.0f), 0.65f, 0.5f, 0.7f, 8, glm::vec3(0.72f, 0.30f, 0.24f));
  md.addBox(at(0.0f, 3.75f, 0.0f), {0.75f, 0.10f, 0.28f}, kWoodLight);
  md.addCylinder(at(0.0f, 4.15f, 0.0f), 0.32f, 0.0f, 0.7f, 8, glm::vec3(0.85f, 0.75f, 0.35f));
  return md;
}

MeshData storagePad() {
  MeshData md;
  md.addCylinder(at(0.0f, 0.12f, 0.0f), 4.6f, 4.4f, 0.24f, 14, glm::vec3(0.52f, 0.44f, 0.34f));
  return md;
}

MeshData woodPile() {
  MeshData md;  // a pyramid of horizontal logs; scaled by stock at draw time
  const glm::vec3 bark(0.42f, 0.30f, 0.18f);
  for (int row = 0; row < 3; ++row) {
    int count = 3 - row;
    for (int k = 0; k < count; ++k) {
      float x = (static_cast<float>(k) - static_cast<float>(count - 1) * 0.5f) * 0.62f;
      glm::mat4 m = at(x, 0.30f + row * 0.52f, 0.0f);
      m = glm::rotate(m, 1.5708f, glm::vec3(1, 0, 0));  // lie along Z
      md.addCylinder(m, 0.28f, 0.28f, 1.9f, 7, row == 1 ? kWoodLight : bark);
    }
  }
  return md;
}

MeshData foodPile() {
  MeshData md;  // grain heaps
  const glm::vec3 grain(0.83f, 0.68f, 0.30f);
  md.addCylinder(at(-0.5f, 0.35f, 0.2f), 0.55f, 0.0f, 0.7f, 8, grain);
  md.addCylinder(at(0.45f, 0.42f, -0.25f), 0.62f, 0.0f, 0.84f, 8, grain * 1.06f);
  md.addCylinder(at(0.15f, 0.30f, 0.55f), 0.45f, 0.0f, 0.6f, 8, grain * 0.94f);
  return md;
}

MeshData campfire() {
  MeshData md;
  for (int k = 0; k < 6; ++k) {
    float a = static_cast<float>(k) * 1.047f;
    md.addRock(at(std::cos(a) * 1.1f, 0.18f, std::sin(a) * 1.1f), 0.26f,
               77u + static_cast<std::uint32_t>(k), kStone);
  }
  for (int k = 0; k < 3; ++k) {
    glm::mat4 m = atRotY(0.0f, 0.55f, 0.0f, static_cast<float>(k) * 2.094f);
    m = glm::rotate(m, 0.5f, glm::vec3(1, 0, 0));
    md.addCylinder(m, 0.11f, 0.09f, 1.3f, 6, kWood);
  }
  return md;
}

MeshData campfireFlame() {
  MeshData md;
  md.addCylinder(at(0.0f, 0.75f, 0.0f), 0.42f, 0.0f, 1.5f, 8, glm::vec3(1.0f, 0.62f, 0.15f));
  md.addCylinder(at(0.0f, 0.55f, 0.0f), 0.24f, 0.0f, 1.0f, 6, glm::vec3(1.0f, 0.85f, 0.35f));
  return md;
}

MeshData fieldSlab(float halfX, float halfZ) {
  MeshData md;
  md.addBox(at(0.0f, 0.06f, 0.0f), {halfX, 0.10f, halfZ}, glm::vec3(0.30f, 0.22f, 0.15f));
  return md;
}

MeshData cropCone() {
  MeshData md;  // unit crop, scaled by growth at draw time
  md.addCylinder(at(0.0f, 0.45f, 0.0f), 0.30f, 0.0f, 0.9f, 6, glm::vec3(0.45f, 0.62f, 0.20f));
  return md;
}

// ------------------------------------------------- the scaffold-built roster

MeshData largeAbode() {
  MeshData md;
  const float hw = 3.1f, hd = 2.4f;
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2)
      md.addBox(at(sx * hw, 1.4f, sz * hd), {0.16f, 1.4f, 0.16f}, kWood);
  md.addBox(at(0.0f, 1.3f, -hd), {hw, 1.2f, 0.11f}, kPlaster);
  md.addBox(at(-hw, 1.3f, 0.0f), {0.11f, 1.2f, hd}, kPlaster);
  md.addBox(at(hw, 1.3f, 0.0f), {0.11f, 1.2f, hd}, kPlaster);
  md.addBox(at(-hw * 0.62f, 1.3f, hd), {hw * 0.38f, 1.2f, 0.11f}, kPlaster);
  md.addBox(at(hw * 0.62f, 1.3f, hd), {hw * 0.38f, 1.2f, 0.11f}, kPlaster);
  md.addBox(at(0.0f, 2.05f, hd), {hw * 0.24f, 0.45f, 0.11f}, kPlaster);
  md.addBox(at(0.0f, 0.85f, hd - 0.04f), {hw * 0.20f, 0.75f, 0.05f},
            glm::vec3(0.12f, 0.09f, 0.06f));
  glm::mat4 l = at(-hw * 0.52f, 3.15f, 0.0f);
  l = glm::rotate(l, 0.66f, glm::vec3(0, 0, 1));
  md.addBox(l, {hw * 0.80f, 0.10f, hd + 0.8f}, kThatch);
  glm::mat4 r = at(hw * 0.52f, 3.15f, 0.0f);
  r = glm::rotate(r, -0.66f, glm::vec3(0, 0, 1));
  md.addBox(r, {hw * 0.80f, 0.10f, hd + 0.8f}, kThatch);
  md.addBox(at(0.0f, 3.78f, 0.0f), {0.20f, 0.11f, hd + 0.7f}, kWood);
  return md;
}

MeshData workshop() {
  MeshData md;
  // Open-sided work shed: posts, low walls, flat tilted roof, a bench + saw.
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2)
      md.addBox(at(sx * 2.4f, 1.25f, sz * 1.9f), {0.15f, 1.25f, 0.15f}, kWood);
  md.addBox(at(0.0f, 0.55f, -1.9f), {2.4f, 0.45f, 0.10f}, kWoodLight);
  md.addBox(at(-2.4f, 0.55f, 0.0f), {0.10f, 0.45f, 1.9f}, kWoodLight);
  glm::mat4 roof = at(0.0f, 2.75f, 0.0f);
  roof = glm::rotate(roof, 0.16f, glm::vec3(0, 0, 1));
  md.addBox(roof, {2.9f, 0.10f, 2.4f}, kThatch);
  md.addBox(at(0.6f, 0.75f, 0.4f), {1.1f, 0.10f, 0.55f}, kWoodLight);  // bench
  for (int k = 0; k < 2; ++k)
    md.addBox(at(0.6f, 0.375f, 0.4f + (k ? 0.4f : -0.4f)), {0.08f, 0.375f, 0.08f}, kWood);
  md.addCylinder(at(-1.2f, 0.45f, 0.8f), 0.32f, 0.32f, 0.9f, 7, kWood);  // log stock
  return md;
}

MeshData store() {
  MeshData md;
  // A walled granary: stone base, plaster walls, shelf lines, wide flat roof.
  md.addBox(at(0.0f, 0.3f, 0.0f), {2.6f, 0.3f, 2.2f}, kStone);
  md.addBox(at(0.0f, 1.5f, 0.0f), {2.3f, 0.9f, 1.9f}, kPlaster);
  md.addBox(at(0.0f, 1.1f, 1.92f), {1.4f, 0.35f, 0.06f}, kWood);   // shelf front
  md.addBox(at(0.0f, 2.0f, 1.92f), {1.4f, 0.12f, 0.06f}, kWood);
  md.addBox(at(0.0f, 2.75f, 0.0f), {2.9f, 0.14f, 2.5f}, kThatch);
  md.addBox(at(0.0f, 3.1f, 0.0f), {1.6f, 0.22f, 1.3f}, kThatch);
  return md;
}

MeshData creche() {
  MeshData md;
  // A round nursery hut with a soft dome.
  md.addCylinder(at(0.0f, 1.0f, 0.0f), 2.1f, 1.9f, 2.0f, 10, kPlaster);
  md.addCylinder(at(0.0f, 2.45f, 0.0f), 2.2f, 0.4f, 1.1f, 10, kThatch);
  md.addBox(at(0.0f, 0.7f, 2.0f), {0.5f, 0.7f, 0.12f}, glm::vec3(0.12f, 0.09f, 0.06f));
  md.addCylinder(at(0.0f, 3.2f, 0.0f), 0.14f, 0.10f, 0.5f, 6, kWood);
  md.addBox(at(0.0f, 3.5f, 0.0f), {0.30f, 0.16f, 0.05f}, glm::vec3(0.95f, 0.75f, 0.4f));
  return md;
}

MeshData graveyard() {
  MeshData md;
  // A fenced plot with a few headstones - symbolic until mortality lands.
  for (int k = 0; k < 4; ++k) {
    float x = -2.1f + 1.4f * static_cast<float>(k);
    md.addBox(at(x, 0.45f, -2.2f), {0.08f, 0.45f, 0.08f}, kWood);
    md.addBox(at(x, 0.45f, 2.2f), {0.08f, 0.45f, 0.08f}, kWood);
  }
  md.addBox(at(0.0f, 0.72f, -2.2f), {2.8f, 0.06f, 0.05f}, kWoodLight);
  md.addBox(at(0.0f, 0.72f, 2.2f), {2.8f, 0.06f, 0.05f}, kWoodLight);
  md.addBox(at(-0.7f, 0.5f, -0.6f), {0.32f, 0.5f, 0.10f}, kStone);
  md.addBox(at(0.9f, 0.42f, 0.4f), {0.30f, 0.42f, 0.10f}, kStone);
  md.addBox(at(-0.2f, 0.38f, 1.1f), {0.26f, 0.38f, 0.10f}, kStone * 0.9f);
  md.addCylinder(at(1.6f, 0.9f, -1.2f), 0.10f, 0.08f, 1.8f, 6, kWood);
  md.addBox(at(1.6f, 1.45f, -1.2f), {0.34f, 0.08f, 0.06f}, kWood);  // marker cross
  return md;
}

MeshData dispenser() {
  MeshData md;
  // A pedestal cradling an orb - worship overflow charges it.
  md.addCylinder(at(0.0f, 0.3f, 0.0f), 1.6f, 1.4f, 0.6f, 9, kStone);
  md.addCylinder(at(0.0f, 1.3f, 0.0f), 0.55f, 0.75f, 1.4f, 8, kStone * 1.08f);
  md.addRock(at(0.0f, 2.55f, 0.0f), 0.62f, 991u, glm::vec3(0.55f, 0.85f, 1.0f));
  return md;
}

MeshData wonder() {
  MeshData md;
  // A monumental spiral of stone rising to a gold crown.
  md.addBox(at(0.0f, 0.5f, 0.0f), {3.4f, 0.5f, 3.4f}, kStone);
  md.addBox(at(0.0f, 1.4f, 0.0f), {2.6f, 0.45f, 2.6f}, kStone * 1.06f);
  for (int k = 0; k < 5; ++k) {
    float a = static_cast<float>(k) * 1.256f;
    float h = 2.4f + static_cast<float>(k) * 1.05f;
    glm::mat4 m = atRotY(std::cos(a) * 1.5f, h, std::sin(a) * 1.5f, -a);
    md.addBox(m, {0.55f, 0.65f, 0.55f}, k % 2 ? kStone : kStone * 1.1f);
  }
  md.addCylinder(at(0.0f, 6.2f, 0.0f), 0.5f, 0.28f, 2.4f, 8, kStone);
  md.addCylinder(at(0.0f, 8.0f, 0.0f), 0.65f, 0.0f, 1.2f, 8,
                 glm::vec3(1.0f, 0.85f, 0.40f));
  return md;
}

// ------------------------------------------------------------ resource props

MeshData logProp() {
  MeshData md;  // lies along X, origin at center
  glm::mat4 m = glm::rotate(glm::mat4(1.0f), 1.5708f, glm::vec3(0, 0, 1));
  md.addCylinder(m, 0.26f, 0.26f, 1.8f, 7, kWoodLight);
  return md;
}

MeshData foodBundleProp() {
  MeshData md;  // a plump grain sack
  md.addBox(at(0.0f, 0.0f, 0.0f), {0.34f, 0.26f, 0.30f}, glm::vec3(0.80f, 0.66f, 0.34f));
  md.addBox(at(0.0f, 0.28f, 0.0f), {0.12f, 0.10f, 0.11f}, glm::vec3(0.62f, 0.48f, 0.22f));
  return md;
}

MeshData stumpProp() {
  MeshData md;  // origin at center, ~0.9 tall
  md.addCylinder(glm::mat4(1.0f), 0.34f, 0.30f, 0.9f, 7, kWood);
  return md;
}

MeshData scaffoldProp() {
  MeshData md;  // one lattice unit, ~1.35 tall, origin at its base center
  const float h = 1.35f, w = 0.75f;
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2)
      md.addBox(at(sx * w, h * 0.5f, sz * w), {0.07f, h * 0.5f, 0.07f}, kWoodLight);
  for (int level = 0; level < 2; ++level) {
    float y = level == 0 ? 0.08f : h - 0.08f;
    md.addBox(at(0.0f, y, -w), {w, 0.06f, 0.06f}, kWood);
    md.addBox(at(0.0f, y, w), {w, 0.06f, 0.06f}, kWood);
    md.addBox(at(-w, y, 0.0f), {0.06f, 0.06f, w}, kWood);
    md.addBox(at(w, y, 0.0f), {0.06f, 0.06f, w}, kWood);
  }
  // Diagonal braces sell the lattice.
  glm::mat4 b1 = at(0.0f, h * 0.5f, w);
  b1 = glm::rotate(b1, 0.85f, glm::vec3(0, 0, 1));
  md.addBox(b1, {0.75f, 0.05f, 0.05f}, kWood);
  glm::mat4 b2 = at(0.0f, h * 0.5f, -w);
  b2 = glm::rotate(b2, -0.85f, glm::vec3(0, 0, 1));
  md.addBox(b2, {0.75f, 0.05f, 0.05f}, kWood);
  return md;
}

// ------------------------------------------------------------ thought bubbles

MeshData bubbleHunger() {
  MeshData md;  // a drumstick-ish morsel
  glm::mat4 m = glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0, 0, 1));
  md.addCylinder(m, 0.09f, 0.07f, 0.45f, 6, glm::vec3(0.92f, 0.88f, 0.80f));
  md.addRock(at(0.18f, 0.20f, 0.0f), 0.17f, 5u, glm::vec3(0.78f, 0.45f, 0.25f));
  return md;
}

MeshData bubbleSleep() {
  MeshData md;  // three rising Z-ish chips
  const glm::vec3 c(0.75f, 0.85f, 1.0f);
  md.addBox(at(-0.12f, -0.10f, 0.0f), {0.10f, 0.045f, 0.02f}, c);
  md.addBox(at(0.02f, 0.06f, 0.0f), {0.13f, 0.055f, 0.02f}, c);
  md.addBox(at(0.18f, 0.26f, 0.0f), {0.16f, 0.065f, 0.02f}, c);
  return md;
}

MeshData bubbleFear() {
  MeshData md;  // exclamation mark
  const glm::vec3 c(1.0f, 0.28f, 0.15f);
  md.addBox(at(0.0f, 0.16f, 0.0f), {0.07f, 0.22f, 0.05f}, c);
  md.addBox(at(0.0f, -0.20f, 0.0f), {0.08f, 0.07f, 0.05f}, c);
  return md;
}

}  // namespace models
