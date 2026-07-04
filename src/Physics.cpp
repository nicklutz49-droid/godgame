#include "Physics.h"

#include "Terrain.h"

void integrateTumble(glm::quat& rot, glm::vec3& angVel, float dt) {
  if (glm::length(angVel) > 0.001f) {
    glm::quat dq = glm::quat(0.0f, angVel.x, angVel.y, angVel.z) * rot;
    rot = glm::normalize(rot + dq * (0.5f * dt));
    angVel *= 1.0f / (1.0f + 0.4f * dt);
  }
}

float collideSphereTerrain(glm::vec3& pos, glm::vec3& vel, glm::vec3& angVel,
                           float radius, float groundOffsetFactor,
                           const Terrain& terrain, float restitution,
                           float tangentialKeep, float minBounce, bool& onGround) {
  float ground = terrain.heightAt(pos.x, pos.z) + radius * groundOffsetFactor;
  onGround = false;
  float impact = 0.0f;
  if (pos.y <= ground) {
    pos.y = ground;
    onGround = true;
    glm::vec3 n = terrain.normalAt(pos.x, pos.z);
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) {
      impact = -vn;
      glm::vec3 tangential = vel - n * vn;
      float bounce = -vn * restitution;
      if (bounce < minBounce) bounce = 0.0f;  // stop micro-bouncing
      vel = tangential * tangentialKeep + n * bounce;
      angVel *= 0.7f;
    }
  }
  return impact;
}
