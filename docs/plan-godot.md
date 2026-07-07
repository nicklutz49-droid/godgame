# godgame — the native Godot rebuild

Decision (owner, this session): rebuild the game **natively in Godot 4
first** — idiomatic engine usage, get it playable — then **rework and add
back** the hard-won systems that need more attention as deliberate later
passes. The C++ game stays in this repo as the runnable reference/spec while
the port catches up.

Locked choices:
- **Language: GDScript.** One binary to run, fastest iteration; heavier
  logic ports later.
- **Movement/physics: Godot built-ins.** CharacterBody3D villagers,
  NavigationServer pathfinding, built-in physics for thrown props. This is
  nondeterministic — which is *fine for now*; determinism is a named
  rework-later item, not a day-one constraint.
- **First slice: island + a village + the hand.** No rival, miracles,
  worship economy, or save yet.

## 0. Environment caveat (read first)

Godot can't be installed in the CI/agent environment (egress policy blocks
godotengine.org and the release hosts). So the Godot project is authored as
text and **verified by the owner running it locally** — like `build.bat`
was. Build in small runnable increments; confirm each in the editor before
piling on. Godot files that ARE safe to author blind: `project.godot`,
`.tscn`/`.tres` (text), `.gd`. Prefer building scenes **from GDScript** in
`_ready()` so hand-authored `.tscn` stays trivial (fewer blind-authoring
foot-guns).

## 1. Layout

```
godot/                 the Godot 4 project (this is the new game)
  project.godot
  Main.tscn            trivial: one Node3D + Main.gd
  Main.gd              builds the world from code
  README.md            how to open & run
src/, docs/, *.md      the C++ game stays as the reference spec
```
`.godot/` (editor cache) is gitignored.

## 2. What Godot gives us for free (stop hand-rolling these)

- **Rendering**: real shadows, GI, PBR, post — our whole M9 hand-built
  shadow map / point-light gather / water / AO becomes engine features.
- **Camera, input, UI**: Camera3D, the input map, Control-node menus/HUD
  replace the M7 pixel-font shell.
- **Physics & navigation**: CharacterBody3D + NavigationServer replace our
  steering + single-sphere physics (at the cost of determinism — see §5).
- **Audio**: AudioStreamPlayer3D + buses replace the M12 mixer; procedural
  synthesis can stay via AudioStreamGenerator if we want it.
- **Assets & export**: glTF import, and one-click Windows/Mac/Linux/web/
  mobile export.

## 3. Slice roadmap to parity (native, GDScript, built-ins)

Each slice is runnable and owner-verified before the next.

- **G1 — the island** (this commit): procedural heightfield terrain
  (FastNoiseLite), water plane, sky/fog/sun, an orbit camera. Confirms the
  toolchain runs and the terrain approach reads well.
- **G2 — a village**: a few buildings (placeholder meshes / CSG or imported),
  a totem, placed on the terrain; static for now.
- **G3 — villagers**: CharacterBody3D people wandering with NavigationServer,
  a couple of jobs (gather wood/food), day/night.
- **G4 — the hand**: pick up / drop / throw props and villagers (built-in
  physics + a grab raycast), the core B&W feel.
- **G5 — worship & belief**: totem → worshippers → mana; the belief number.
- **G6 — the rival & conversion**, then **G7 — miracles**, **G8 — save/UI
  shell**. These mirror the C++ milestones but built the Godot way.

## 4. Mapping the C++ systems onto Godot (reference)

| C++ system | Godot-native approach |
|---|---|
| `Terrain` heightfield | FastNoiseLite → ArrayMesh (or HeightMapShape3D for collision) |
| `MeshData` procedural models | build with `SurfaceTool`, or import glTF later |
| villager steering + physics | CharacterBody3D + NavigationAgent3D |
| the hand (`Hand`) | raycast pick + reparent, `apply_impulse` for throws |
| `World::events` → sound/effects | Godot signals + AudioStreamPlayer3D + GPUParticles |
| M9 shadows/lights/water | Environment + DirectionalLight3D + OmniLight3D + water shader |
| M7 menus/HUD (pixel font) | Control nodes + Theme |
| `.gmap`/`.sav` | `Resource` + `ResourceSaver`, or keep a custom binary writer |
| M12 sound synth | AudioStreamGenerator (keep the synth) or baked `.wav` |
| M10 B&W overlay loaders | port the LND/L3D/G3D/DXT parsers to GDScript or a GDExtension |

## 5. Rework-back list — the systems that need attention *later*

These are the things we deliberately drop for native-first and re-introduce
as focused passes, each with its own plan:

1. **Determinism / lockstep.** Godot's physics and nav are not bit-exact or
   cross-platform reproducible. When we want the checksum, the
   continue-in-lockstep save, and the AI-vs-AI balance sweeps back, the
   rework is: pull the movement/collision of *gameplay-relevant* actors out
   of Godot physics into a deterministic fixed-step core (ported from the
   C++ sim, in GDScript or a C#/GDExtension module), keeping Godot physics
   only for cosmetic debris. Decide this when balance tooling matters again.
2. **The headless self-test suite.** Our 260-check C++ suite doesn't port
   directly. Rework: GdUnit4/GUT for GDScript logic + `godot --headless` in
   CI. Weaker than the C++ suite until the deterministic core (1) exists.
3. **Complete-state save + map editor.** Depends on (1) for the lockstep
   guarantee; until then, saves are best-effort, not bit-exact.
4. **The B&W runtime overlay (M10).** The clean-room loaders port to
   GDScript/GDExtension; the invariant (asset bytes never shipped, read from
   the owner's install at runtime) is preserved by architecture, unchanged.
5. **Procedural audio (M12).** Reimplement the synth on AudioStreamGenerator,
   or bake the samples to `.wav` at build time. Positional/ambience via buses.
6. **llvmpipe / minimum-hardware.** Forward+ wants a GPU; if software-render
   playability matters, switch the renderer to Compatibility (GL ES 3).

## 6. Open cross-checks

- The scoping workflow (six-subsystem research + adversarial verify) will
  land with an independent methodology; reconcile its corrections into §4/§5
  when it does. Its live-doc research was blocked by the same egress policy,
  so weight it as model-knowledge, not sourced.
- First owner action: open `godot/` in Godot 4.x, press Play, confirm the
  island renders and the camera orbits. Report anything that doesn't, and
  we iterate from a proven-running base.
