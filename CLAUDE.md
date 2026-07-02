# godgame — development notes

Black & White inspired god game in C++20 / OpenGL 3.3 / SDL2. No creature.
All art is procedural placeholder; original B&W assets may be wired in later
behind the existing interfaces (Terrain, Mesh). Personal project, no
distribution.

## Build & test

```sh
cmake -B build -G Ninja && cmake --build build
./build/godgame --headless          # world-gen + physics self-test, exits 0 on OK
xvfb-run -a ./build/godgame --screenshot /tmp/shot.bmp 90 far   # visual check
```

Cross-platform (Linux dev, Windows target). CMake falls back to FetchContent
for SDL2/glm when system packages are missing — don't add hard system deps.

## Architecture invariants

- `gl.h` is a hand-rolled GL 3.3 core loader; call functions as `gl.Enable(...)`.
  Add new GL entry points to `GL_FUNC_LIST` — do not include system GL headers.
- All meshes share one vertex layout: position(3) normal(3) color(3).
  Placeholder models are built via `MeshData::add*` and drawn with the single
  "lit" shader in main.cpp (uniforms: uModel/uVP/uSunDir/uCamPos/uFogColor/
  uFogDensity/uAlpha/uEmissive). Shaders are embedded C++ raw strings.
- Terrain/World/Hand never touch GL — `--headless` must keep working without
  a window. Rendering lives in main.cpp, Water, Sky.
- World generation must stay deterministic per seed and identical across
  platforms: use `noise::*` and the XorShift in World.cpp, never std::rand,
  std distributions, Date/time.
- `World::kTreeHalfHeight` must match the tree model built in
  `buildTreeMeshData()` (model spans y ∈ [-3.25, 3.25], origin at center).
- Front faces are CCW; back-face culling is on. New primitive builders need
  outward CCW winding (see the cylinder in Mesh.cpp for the pattern).

## Conventions

- Y is up. World units roughly meters; island is `Terrain::SIZE` = 512 across,
  water plane at y = 0, seabed at −14.
- Physics: single sphere vs heightfield per prop, no prop-prop collision yet.
- Keep everything working on llvmpipe (no GL extensions beyond 3.3 core).
