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
