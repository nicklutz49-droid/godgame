#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Terrain;

// The ballistic math shared by thrown props and thrown villagers, extracted
// verbatim from the original World::update prop loop so both feel identical
// under the hand. Water behavior stays type-specific and outside.

// Airborne tumble: integrate rot by angVel and damp it (exact prop behavior).
void integrateTumble(glm::quat& rot, glm::vec3& angVel, float dt);

// Sphere-vs-heightfield contact: snaps onto the ground, reflects velocity with
// restitution, damps tangential motion and spin. Returns the impact speed
// (the negative normal velocity at contact, before the bounce) or 0 if there
// was no contact this step. `groundOffsetFactor` is how deep the sphere rests
// (props use 0.55). Sets onGround when the body ended the step in contact.
float collideSphereTerrain(glm::vec3& pos, glm::vec3& vel, glm::vec3& angVel,
                           float radius, float groundOffsetFactor,
                           const Terrain& terrain, float restitution,
                           float tangentialKeep, float minBounce, bool& onGround);
