# godgame — the Godot port (hybrid GDExtension)

Decision (owner, this session): bring the game to **Godot 4 as a hybrid** —
keep all of `src/` compiled **unchanged** as an engine-free deterministic
C++ library (`godgame_sim`), expose it to Godot through a thin `godot-cpp`
**GDExtension** bridge, and rebuild only the *view/shell* (renderer, water,
sky, HUD, menus, audio device, input) as Godot nodes on the **Compatibility
(OpenGL 3.3)** renderer. This preserves every crown jewel — the FNV
`worldChecksum`, the lockstep save/continue-equality, `.sav`/`.gmap`
byte-stability, AI-vs-AI reproducibility, the `bw` overlay loaders, and the
260-check headless suite — because the checksummed bytes never leave the C++
that produces them. Godot is never in the sim loop, so its non-deterministic
physics/RNG/navigation are simply never used.

This supersedes the earlier "native-first GDScript rewrite" framing (and the
`godot/` GDScript island prototype, G1 — kept as a throwaway toolchain
smoke test). The independent scoping workflow reached the same conclusion:
a rewrite would re-author ~10k lines of float sim under *weaker* determinism
guarantees; the hybrid makes the whole determinism question moot.

## 0. Environment caveat

Godot can't be installed in the agent environment (egress policy blocks
godotengine.org and the release hosts, and godot-cpp fetch may be blocked
too). So: **Phase 1 (the C++ library split) is fully built and verified
here** — it's pure C++/CMake and the 260-check suite is the gate. The
Godot-side pieces (the GDExtension bridge that needs godot-cpp headers, and
the Godot view project) are **authored as text and verified by the owner
running them locally**, like `build.bat` was.

## 1. Path chosen, and why not the others

- **Path A — native rewrite (C#/GDScript): rejected.** Godot Jolt disclaims
  determinism, NavigationServer avoidance is async on a threadpool, and
  neither GDScript nor the .NET JIT gives fixed-order/contraction-controlled
  float bit-identity. A rewrite retires determinism, the lockstep save, the
  AI-vs-AI tooling, and the headless suite — and "rework them back later" is
  a from-scratch fixed-point sim, not a light pass.
- **Path B — hybrid GDExtension: chosen.** The model/view firewall the
  codebase already enforces (sim never touches GL/SDL; talks out only via
  `World::events` + the checksum) *becomes* the extension boundary for free.
- **Path C — stay in C++: the baseline** the port must out-earn (it buys the
  Godot renderer, editor, UI, and one day easier art/import).

Trade accepted: Godot is a view over a C++ core, so we do **not** use its
physics/nav/particles for gameplay, and web/mobile export is harder (native
code). If those ever outrank determinism, revisit Path A.

## 2. Phases (P1 done; P1b–P5 ahead)

### P0 — pinned constraints (ratified)
- **Compatibility (GL 3.3) renderer** to preserve the llvmpipe/GL-3.3-core
  invariant (Forward+/Mobile are Vulkan; crash on software rasterizers).
- Pin one Godot 4.x to a matching `godot-cpp` tag; "rebuild + re-run the
  full suite" is part of every engine upgrade (GDExtension is
  forward-compatible only).
- `glm` stays the sim's internal math type; convert to `godot::Vector3`
  **one-way, outbound, at the render boundary only**.
- No `-ffast-math`/`-Ofast`; SSE2 (no x87); the sim lib and the bridge
  compile with matching C++20/ABI/runtime-library flags.
- The sim stays **single-threaded** and off Godot's worker threads.

### P1 — engine-free `godgame_sim` static lib ✅ (built & verified here)
`Mesh.cpp` split into `MeshData.cpp` (CPU builders, in the lib) and
`Mesh.cpp` (the GL class, app-side). New `godgame_sim` static library:
`World / Village / Villagers / GodAI / Terrain / Hand / Physics / MapFile /
SaveFile / MeshData / bw/*` — **provably zero GL and zero SDL symbols**
(`nm` clean). The app links it; the 260-check suite stays green; the render
path is unchanged (village screenshot identical workflow). This is the
library the GDExtension will link.

### P1b — standalone headless test/CLI binary (next, verifiable here)
Extract `worldChecksum` / `runHeadless` / `runMatch` from `main.cpp` into
`SimCore.cpp`; add a `godgame_headless` binary that links **only**
`godgame_sim` (+ `Sound.cpp` for test [20]). Makes the determinism proofs
the engine-free CI gate — faster than booting Godot, and it never touches
the view.

### P2 — the `godot-cpp` GDExtension bridge (author here, compile locally)
One bridge `Node3D` owns a `World` by value. Drive `world.update(1.0f/60.0f)`
from a **fixed accumulator inside `_physics_process`** (never `_process`,
never Godot's `delta`; loop the fixed step for the F4 sim-speed feature).
Read sim state each frame; convert `glm → Vector3` **outbound only**. The
bridge does **zero** sim arithmetic — no Godot `Variant`/`Transform3D` math,
`RandomNumberGenerator`, `PhysicsServer`, `NavigationServer`, or `Timer`
touches anything that reaches the checksum or a save. Keep `Hand`'s own
ray-vs-heightfield pick (do not adopt Godot colliders for input).

### P3 — the render layer (Godot, Compatibility renderer)
Marshal packed transforms to `MultiMesh` via `RenderingServer` (scale is
tiny: pop cap 24, a few hundred props). Terrain from `Terrain`'s heightfield
→ `ArrayMesh`. Port the M9 look with Godot's own shadows/lights/water. Draw
paths read sim state; they never write it.

### P4 — the shell (Godot Control nodes)
Menus/HUD/editor as `Control` + `Theme`, driven by the same
`World`/`GodAI`/`savefile::` state. `.gmap`/`.sav` keep their byte formats
(the C++ writers stay authoritative).

### P5 — audio & input device layer
Re-author the M12 device/mixer on Godot audio buses + `AudioStreamPlayer3D`,
fed by the same `World::events` seam. `snd::bake()` synthesis can stay
(procedural) or bake to `.wav`. Input events map to the existing `Hand`
verbs at the bridge.

## 3. C++ → Godot mapping (reference)

| C++ | Godot (view only; sim stays C++) |
|---|---|
| `Terrain` heightfield | `ArrayMesh` + `HeightMapShape3D` (visual/collision only) |
| `MeshData` builders | marshal to `ArrayMesh`/`MultiMesh` |
| `World::events` seam | drive `AudioStreamPlayer3D` + `GPUParticles3D` |
| M9 shadows/lights/water | `Environment` + `DirectionalLight3D` + `OmniLight3D` + water shader |
| M7 menus/HUD (pixel font) | `Control` nodes + `Theme` |
| `.gmap`/`.sav` writers | unchanged C++ (`mapfile::`/`savefile::`) |
| M12 synth + mixer | `AudioStreamGenerator`/buses (keep synth) |
| `bw` overlay loaders | unchanged C++, called through the bridge |

## 4. What must never regress

The 260-check headless suite is the contract. Every phase keeps it green;
the GDExtension is *added around* the sim, never *into* it. If a change
would perturb `worldChecksum`, it belongs in the view, not the sim.
