# godgame — development notes

Black & White inspired god game in C++20 / OpenGL 3.3 / SDL2. No creature.
All art is procedural placeholder; original B&W assets may be wired in later
behind the existing interfaces (Terrain, Mesh). Personal project, no
distribution. Slices so far: living village (docs/plan-villagers.md),
worship/belief/mana/temple + food miracle (docs/plan-worship.md), scaffolds &
the building roster (docs/plan-scaffolds.md), mortality & burial, multi-
village worlds with neutrals, gods & conversion (per-god belief + ownership
ratchet), the rival AI god (docs/plan-rival.md), the map editor & .gmap files
(docs/plan-editor.md), the skirmish shell — menus/HUD/game-saves/temple
collapse (docs/plan-shell.md), the balance & feel pass (difficulty profiles,
pacing sweeps). Master arc complete through M8: docs/plan-game.md; what
remains is docs/plan-next.md (story mode, assets, extensions).

Multi-village invariants: `World::villages[0]` is the player's home village;
indices are stable for the session. Prop `claimedBy`/`carrier` store packed
cross-village ids from `villagerId(village, index)`; farm-cell claims stay
village-local. Divine acts route through `World::notifyDivineEvent` (all
villages); only villages owned by a god project that god's influence or feed
that god's mana.

Gods invariants (M4): gods live in `World::gods[tune::kMaxGods]`
(`gods[0]` = the player); each active god owns a `Temple` and a mana pool —
there is no global temple/mana anymore. Every divine act carries the acting
god: `notifyDivineEvent(god, where, fear, awe)` (god = -1 for unattributed
fear, e.g. deaths). Village ownership changes ONLY through
`World::updateOwnership` → `convertVillage` (the ratchet: neutrals need
belief > kConvertNeutralBelief with a kConvertLeadMargin lead; owned villages
flip only when a challenger clears kStealBelief while the owner is below
kStealOwnerBelow) — never assign `Village::owner` directly. Hand throws stamp
`Prop::thrownByGod`; villager pickup or storage settle credits that god and
clears the stamp. `insideInfluence(p, god)` is per god. The headless checksum
covers per-god beliefs, owners, and mana — keep new god state inside it.

Map invariants (M6): a `.gmap` is a STARTING CONDITION (heightfield + entity
specs), never a mid-game save (full-state saves are M7). `mapfile::load`
rebuilds through the standard founding paths with `plan(..., terraform=false)`
— the saved heightfield already carries every terrace, so layout decisions
(all made after flattening) read identical ground and a loaded map is
bit-for-bit the world that was saved; save → load → save must stay
byte-stable (headless test [14] enforces both). Editor mutations go through
World editor verbs (`editorPlaceVillage`/`editorPlaceTemple`/`editorPaint*`/
`editorEraseProps`) — sim-side, deterministic, no new spawn paths. The editor
freezes the sim (main.cpp never calls `world.update` while `editor`), and
toggling either direction rebuilds the world from the map snapshot.

Shell invariants (M7): a `.sav` (savefile::) is the COMPLETE sim state —
every prop/villager/village/god/AI field including private rng streams
(`SaveIO` is the one friend allowed in). Saves must stay byte-stable
(save → load → save identical) and a loaded game must CONTINUE bit-for-bit
in lockstep with the original (headless test [15] enforces both) — any new
sim-relevant field MUST be added to SaveFile.cpp or [15] fails the lockstep
check. Saving settles every hand first (`GodAI::settle`, held things
released in place) so a save is always a valid world. A god losing its last
village fires `collapseTemple` from inside `updateOwnership` only (rubble
props, `God::ruined`, temple un-founded); never break gods elsewhere. Menus,
HUD, and the pixel font (Font.*) are render-side; the sim never knows the
shell exists. Text is drawn with the lit shader at uEmissive=1, fog 0,
through a pixel ortho — no new shaders, no textures.

Balance invariants (M8): rival difficulty = `tune::kAiProfiles`
(EASY/FAIR/CRUEL) indexed by `GodAI::profile` — cadence/appetite/boldness
only, never cheats; profile survives `reset()` and rides in saves (v2).
Pacing is tuned by `--match [days] --seed S` sweeps at REAL day length
(SUMMARY line per run; target: first conversion day 3-5, no stalled
islands — the relaxed second pass in `findVillageSites` guarantees
contested ground). "Acts or decay" is ratified design: worship sustains
mana, not belief; don't add belief-sustain without a new owner decision.

## Build & test

```sh
cmake -B build -G Ninja && cmake --build build
./build/godgame --headless          # full self-test suite, exits 0 on OK
xvfb-run -a ./build/godgame --screenshot /tmp/shot.bmp 120 village  # visual check
```

The headless suite covers world gen, prop physics, thrown villagers,
drop-to-assign, storage absorption, worship/miracles, scaffolds, mortality,
multi-village worlds, the conversion ratchet, the rival AI (incl. AI-vs-AI
determinism; `--match` runs full wars), map round-trips, game-save
round-trips (incl. lockstep continue-equality), the temple collapse, a 3-day
economy/schedule soak, and a double-run determinism checksum. Keep it green;
extend it with every system.

Cross-platform (Linux dev, Windows target). CMake falls back to FetchContent
for SDL2/glm when system packages are missing — don't add hard system deps.

## Architecture invariants

- `gl.h` is a hand-rolled GL 3.3 core loader; call functions as `gl.Enable(...)`.
  Add new GL entry points to `GL_FUNC_LIST` — do not include system GL headers.
- All meshes share one vertex layout: position(3) normal(3) color(3).
  Placeholder models are built via `MeshData::add*` (main.cpp, Models.cpp) and
  drawn with the "lit" shader in main.cpp (uniforms: uModel/uVP/uSunDir/
  uSunColor/uAmbient/uTint/uCamPos/uFogColor/uFogDensity/uAlpha/uEmissive,
  plus the M9 shadow/light set: uLightVP/uShadow/uShadowStrength/
  uLightCount/uLightPos[6]/uLightCol[6]). Shaders are embedded C++ raw
  strings. M9 adds one depth-only shader for the sun's shadow pass: a 1024
  depth FBO with hardware compare, texture unit 1 is RESERVED for it, the
  ortho box is texel-snapped around the camera focus, and F7 toggles the
  pass (llvmpipe). Casters = terrain/buildings/temples/trees/rocks/stumps;
  villagers and loose props keep blob discs. Emissive draws (text, rings,
  ghosts) bypass shadow and point-light math by design.
- Sim code (Terrain/World/Village/Villagers/Hand/DayCycle/Physics) never
  touches GL — `--headless` must keep working without a window. Rendering
  lives in main.cpp, Water, Sky.
- World generation must stay deterministic per seed and identical across
  platforms: use `noise::*` (incl. `noise::XorShift`), never std::rand,
  std distributions, or time. Village founding avoids libm trig (hardcoded
  direction tables), and any yaw that reaches world/map/save state is
  computed with `noise::atan2det`, never `std::atan2` (libm differs across
  platforms). Runtime steering may use std::atan2 (per-run determinism
  only). Villager runtime sim additionally must be deterministic within a
  run: per-villager XorShift streams, index-ordered updates — the headless
  checksum test enforces this.
- Sim-time quantities that should survive day-length changes (needs, crop
  growth, child growth) advance in day-fraction units; walking/working act in
  real seconds. Headless soaks shrink `DayCycle::secondsPerDay`.
- Mortality is live (M2): every death flows through `villagerKill` reached
  only via the three seams — `applyLanding` (impact), `submergedTime`
  (drowning), the starvation timer. Never add ad-hoc kill paths. Held
  villagers cannot die (the divine grip preserves). Dead villagers keep their
  slot (`alive=false`) — indices stay stable like props and buildings; every
  villager loop must guard on `alive`.
- Props are never erased: consumed props set `alive=false` and slots are
  reused by `World::spawnProp`. Holders (hand, villagers) re-validate on use.
- Buildings are never erased either (villager targets hold indices); dead
  stages: -1 reserved plot, future -2 cancelled. Scaffold placement/combining
  go only through `World::tryPlaceScaffold`/`tryCombineScaffold`, and the
  stack→building mapping lives in `Village::buildingForStack`.
- `World::kTreeHalfHeight` must match the tree model built in
  `buildTreeMeshData()` (model spans y ∈ [-3.25, 3.25], origin at center).
- Front faces are CCW; back-face culling is on. New primitive builders need
  outward CCW winding (see the cylinder in Mesh.cpp for the pattern).
- Gameplay constants belong in `src/Tuning.h`, not inline.

## Conventions

- Y is up. World units roughly meters; island is `Terrain::SIZE` = 512 across,
  water plane at y = 0, seabed at −14. Villager origin is at the feet.
- Physics: single sphere vs heightfield per prop/villager (shared
  `Physics.h` ballistic step), no body-body collision.
- Villager AI: priority ladder (physical > fear > sleep > hunger >
  [communal-worship-reserved] > job > idle) on a staggered 0.4 s think tick;
  jobs (incl. Worshipper, whose dance is continuous) are flat per-job state
  machines in Villagers.cpp. Steering only, no A*.
- Belief/mana flow through fixed funnels: all divine acts call
  `Village::notifyDivineEvent(god, where, fear, awe)`; only the worship dance
  adds mana (`World::gods[owner].mana`); only `World::castFoodMiracle(p, god)`
  (and future miracles) spend it. Influence checks go through
  `World::insideInfluence(p, god)` — the hand and future casts must respect
  it.
- Keep everything working on llvmpipe (no GL extensions beyond 3.3 core).
