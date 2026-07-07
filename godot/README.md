# Godgame — native Godot rebuild

This folder is a ground-up rebuild of the god game in **Godot 4** (GDScript),
the way Godot wants to be built. The C++ game one directory up stays as the
runnable reference/design spec while this catches up. Plan and roadmap:
[../docs/plan-godot.md](../docs/plan-godot.md).

> **Authored untested.** Godot can't run in the environment this was written
> in (egress policy blocks the download), so this project is hand-authored
> against the Godot 4 API and verified by *you* running it. If something
> doesn't load or render, tell me exactly what the editor reported and we
> fix it from a known state — like breaking in a new toolchain.

## Run it

1. Install **Godot 4.x** (standard build — no .NET/C# needed) from
   <https://godotengine.org/download>.
2. Open Godot → **Import** → pick `godot/project.godot` in this repo.
3. Press **Play** (F5). If it asks for a main scene, choose `Main.tscn`.

You should see a procedural island in the sea under a low sun.
Right-drag orbits the camera, the mouse wheel zooms, `WASD` pans.

## What's here (slice G1)

- Procedural heightfield island (FastNoiseLite → a coloured mesh), a water
  plane, sky + fog + a shadow-casting sun, and an orbit camera — all built
  from `Main.gd` so the scene file stays trivial.

## Next

- **G2** a village, **G3** villagers (CharacterBody3D + navigation),
  **G4** the divine hand (grab / drop / throw). See the roadmap.

If G1 renders on your machine, say so and I'll add the village next.
