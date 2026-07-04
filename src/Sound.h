#pragma once

#include <glm/glm.hpp>

#include <cstdint>

// Procedural audio (M12): every sample is synthesized at startup - no asset
// files, no new dependencies, just SDL's audio callback and a tiny voice
// mixer. The sim NEVER calls into here (it speaks through World::events);
// only the app does. Everything is a safe no-op until init() succeeds, so
// --headless and audio-less machines behave identically.
namespace snd {

// One-shot effects.
enum class Sfx : int {
  ThudSoft,   // prop lands
  ThudHard,   // heavy landing / body
  Splash,     // into the water
  Chop,       // axe on wood
  TreeFall,   // felled trunk meets ground
  Scream,     // villager grabbed/thrown/flung
  Whoosh,     // hand throw
  Place,      // gentle set-down
  Clack,      // scaffold placed/combined
  Complete,   // building finished (bright chime)
  Cast,       // miracle release (sparkle)
  Bloom,      // forest takes root
  Explosion,  // fireball impact
  Rumble,     // temple collapse
  Bell,       // village conversion
  UiMove,
  UiSelect,
  UiDeny,
  Count
};

// Looping ambience beds, crossfaded by the app each frame.
enum class Bed : int {
  Wind,      // always, gently
  Birds,     // daylight
  Crickets,  // night
  Fire,      // near campfires at night
  RainBed,   // near an active rain cloud
  Chant,     // worship within earshot
  Count
};

// Synthesize the sample bank (pure math, no SDL). Called by init(), and by
// the headless suite to prove the bakery is deterministic (force rebakes).
void bake(bool force = false);
// FNV over every baked sample - the determinism probe.
std::uint64_t bankChecksum();

// Open the audio device (SDL_INIT_AUDIO is initialized here). False (and
// silent no-op mode) when no device exists. Idempotent.
bool init();
void shutdown();

void setMasterVolume(float v);  // 0..1
float masterVolume();

// One-shots. playAt pans/attenuates against the listener.
void play(Sfx s, float gain = 1.0f, float pitch = 1.0f);
void playAt(Sfx s, const glm::vec3& worldPos, float gain = 1.0f,
            float pitch = 1.0f);

// Ambience: set each bed's target gain (0..1) every frame; the mixer eases
// toward it so starts/stops never click.
void bed(Bed b, float targetGain);

// The ear: camera position and forward, once per frame.
void setListener(const glm::vec3& pos, const glm::vec3& forward);

}  // namespace snd
