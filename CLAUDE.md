# godgame — development notes

Black & White inspired god game in C++20 / OpenGL 3.3 / SDL2. No creature.
All art is procedural placeholder; original B&W assets may be wired in later
behind the existing interfaces (Terrain, Mesh). Personal project, no
distribution. Slices so far: living village (docs/plan-villagers.md),
worship/belief/mana/temple + food miracle (docs/plan-worship.md), scaffolds &
the building roster (docs/plan-scaffolds.md). Master arc: docs/plan-game.md.

## Build & test

```sh
cmake -B build -G Ninja && cmake --build build
./build/godgame --headless          # full self-test suite, exits 0 on OK
xvfb-run -a ./build/godgame --screenshot /tmp/shot.bmp 120 village  # visual check
```

The headless suite covers world gen, prop physics, thrown villagers,
drop-to-assign, storage absorption, a 3-day economy/schedule soak, and a
double-run determinism checksum. Keep it green; extend it with every system.

Cross-platform (Linux dev, Windows target). CMake falls back to FetchContent
for SDL2/glm when system packages are missing — don't add hard system deps.

## Architecture invariants

- `gl.h` is a hand-rolled GL 3.3 core loader; call functions as `gl.Enable(...)`.
  Add new GL entry points to `GL_FUNC_LIST` — do not include system GL headers.
- All meshes share one vertex layout: position(3) normal(3) color(3).
  Placeholder models are built via `MeshData::add*` (main.cpp, Models.cpp) and
  drawn with the single "lit" shader in main.cpp (uniforms: uModel/uVP/uSunDir/
  uSunColor/uAmbient/uTint/uCamPos/uFogColor/uFogDensity/uAlpha/uEmissive).
  Shaders are embedded C++ raw strings.
- Sim code (Terrain/World/Village/Villagers/Hand/DayCycle/Physics) never
  touches GL — `--headless` must keep working without a window. Rendering
  lives in main.cpp, Water, Sky.
- World generation must stay deterministic per seed and identical across
  platforms: use `noise::*` (incl. `noise::XorShift`), never std::rand,
  std distributions, or time. Village founding avoids libm trig (hardcoded
  direction tables). Villager runtime sim additionally must be deterministic
  within a run: per-villager XorShift streams, index-ordered updates — the
  headless checksum test enforces this.
- Sim-time quantities that should survive day-length changes (needs, crop
  growth, child growth) advance in day-fraction units; walking/working act in
  real seconds. Headless soaks shrink `DayCycle::secondsPerDay`.
- Villagers are invulnerable this slice. Every future death must flow through
  `applyLanding` / the starvation clamp / `submergedTime` (the marked
  MORTALITY SEAMs) — do not add ad-hoc kill paths.
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
  `Village::notifyDivineEvent(where, fear, awe)`; only the worship dance adds
  mana (`World::temple.mana`); only `World::castFoodMiracle` (and future
  miracles) spend it. Influence checks go through `World::insideInfluence` —
  the hand and future casts must respect it.
- Keep everything working on llvmpipe (no GL extensions beyond 3.3 core).
