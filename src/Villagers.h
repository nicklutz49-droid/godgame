#pragma once

#include <glm/glm.hpp>

#include <cstdint>

class World;
struct Villager;

// What the divine hand is pointing at / holding.
struct GrabTarget {
  enum class Kind : std::uint8_t { None, Prop, Villager };
  Kind kind = Kind::None;
  int village = -1;  // which village the villager belongs to (villagers only)
  int index = -1;

  bool none() const { return kind == Kind::None; }
  bool isProp() const { return kind == Kind::Prop; }
  bool isVillager() const { return kind == Kind::Villager; }
  void clear() { kind = Kind::None; village = -1; index = -1; }
};

// Prop claims/carriers must be unambiguous across villages: a packed id.
inline int villagerId(int village, int index) { return village * 4096 + index; }

// --- sim entry points (no GL) ---

// Brains, needs, locomotion, and the physical states of every villager,
// village by village.
void villagersUpdate(World& world, float dt);

// The hand closed around a villager.
void villagerGrabbed(World& world, int villageIdx, int idx);

// The hand let go. gentle = drop-to-assign resolves at the landing point;
// otherwise it is a throw (ballistic, flailing, panic on recovery).
void villagerReleased(World& world, int villageIdx, int idx,
                      const glm::vec3& velocity, bool gentle);

// Ray-pick over props AND villagers; villagers win near-ties.
GrabTarget pickTarget(const World& world, const glm::vec3& origin,
                      const glm::vec3& dir, float maxDist);

// A felled tree finished falling: swap it to a stump and pop out logs.
void convertFelledTree(World& world, int propIdx);

// --- render-side pose (pure math, testable headless) ---

struct VillagerPose {
  glm::mat4 root{1.0f};  // yaw / crouch / lying, applied after translate(pos)
  glm::mat4 head{1.0f}, torso{1.0f}, armL{1.0f}, armR{1.0f}, legL{1.0f}, legR{1.0f};
};
VillagerPose computeVillagerPose(const Villager& v, float time);
