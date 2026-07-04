# godgame — the handoff plan: everything that remains

Written so that any capable developer (or model) can pick this project up
cold and finish it. Read this alongside:

- **[CLAUDE.md](../CLAUDE.md)** — the working rules. Non-negotiable
  invariants (sim/render firewall, determinism, funnels, stable indices,
  byte-stable file round-trips, the lockstep-continue save guarantee).
- **[plan-game.md](plan-game.md)** — the vision, pillars, and the milestone
  arc (M1-M7 shipped; M8 and the story arc remain).
- Per-slice designs: plan-villagers.md, plan-worship.md, plan-scaffolds.md,
  plan-rival.md, plan-editor.md, plan-shell.md.
- **[plan-assets.md](plan-assets.md)** — importing original Black & White
  assets (companion to this document).

## How to work on this codebase

```sh
cmake -B build -G Ninja && cmake --build build   # must stay 0 warnings
./build/godgame --headless                        # 174 checks, exits 0 on OK
./build/godgame --match 14                        # AI-vs-AI balance report
xvfb-run -a ./build/godgame --screenshot s.bmp 240 village   # visual check
```

The delivery discipline that built M1-M7 — keep it:

1. Write `docs/plan-<slice>.md` first (goal, design, tests, out-of-scope).
2. Implement sim-side first, render-side second; never let sim code touch GL.
3. Extend the headless suite with a numbered section proving the slice; any
   new sim-relevant state goes into `worldChecksum`, `SaveFile.cpp`, and (if
   it exists at t=0) `MapFile.cpp`. Test [15]'s lockstep check will fail if
   you forget SaveFile — that's by design.
4. Screenshot-verify on llvmpipe. Update README ("Current slice" pattern),
   CLAUDE.md invariants, then commit with a story-telling message.

The load-bearing funnels (never bypass, always extend):

| Concern | The one path |
| --- | --- |
| Divine acts → belief/fear | `World::notifyDivineEvent(god, where, fear, awe)` |
| Ownership flips | `updateOwnership` → `convertVillage` (+ `collapseTemple`) |
| Deaths | `villagerKill` via applyLanding / submergedTime / starvation |
| Mana in / out | worship dance in Villagers.cpp / `castFoodMiracle` |
| Reach | `World::insideInfluence(p, god)` |
| Building placement | `tryPlaceScaffold` / `tryCombineScaffold` |
| Founding | `Village::plan` + `spawnVillagers` (maps load through these) |
| Structure damage (future) | `applyStructureDamage` — see Temple Assault below |

---

## 1. M8 — balance & feel pass (SHIPPED 2026-07)

> Landed: real-pace `--match` with SUMMARY lines, 8-seed sweeps (first
> conversion day 2-4 median 3, zero stalled islands via the relaxed site
> pass), EASY/FAIR/CRUEL `AiProfile` presets on the menu (save v2),
> "acts or decay" ratified, collapse/conversion/cast juice, corpse-scan
> hoist. Remaining ideas below stay valid for future passes.

**Goal**: "fun on purpose." The systems all exist; make the 1v1 war paced,
readable, and performant.

**Balance workflow** — the tooling exists, use it:
- `--match N` runs deterministic AI-vs-AI wars with day-by-day reports
  (villages, belief, mana, acts). Sweep seeds: run 10+ seeds × 20 days,
  tabulate day-of-first-conversion, winner, final belief curves. A
  `--match-csv` flag writing one row per day would make this scriptable —
  add it (30 lines in `runMatch`).
- Knobs live in `src/Tuning.h` exclusively. The ones that shape the war:
  `kAweGift/kAweGiftThrown` (gift conversion pressure), `kBeliefDecayPerDay`
  vs `kBeliefFromWorshipPerDay` (owner-belief equilibrium — currently sags
  to the floor at ~2 dancers; decide if that's intended), `kAiThinkPeriod/
  kAiHandSpeed/kAiActCooldown` (rival tempo), `kConvertNeutralBelief/
  kStealBelief/kStealOwnerBelow` (ratchet stickiness), `kManaPerWorshipperPerDay`.
- Known balance facts from M5 testing: gifts credit ~0.02-0.03 belief each
  (witness-scaled; zero at night — intended), the rival converts the nearest
  neutral around day 2 at current numbers, and miracle-courting only becomes
  possible once rings overlap (Center upgrades add +8 m/level, temple ring
  is fixed 70 m, villages are 130 m+ apart — so gifts carry the early war
  by design).

**Difficulty presets**: add `struct AiProfile { thinkPeriod, handSpeed,
actCooldown, aggression, foodReserve }` consumed by GodAI instead of raw
tune constants; EASY/FAIR/CRUEL selected on the title menu's skirmish row
(store on `God` or `GodAI`, serialize in SaveFile — remember test [15]).

**Feel/juice list** (render-side only, no sim changes):
- Conversion ceremony: totem repaint flash + villager tunic re-accents
  already exist via owner tint; add a belief-colored pulse column and a
  2-3 s slow camera nudge toward the flipped village.
- Temple collapse: dust puffs (smokeDisc sprites) + brief screen shake
  (camera offset decay), sound hook (see Sound below).
- Hand feedback: a soft ring pulse on successful cast; red flash when a
  cast fails for mana/influence.
- HUD: mana bar as a diegetic strip under the ledger; belief per village on
  hover (raycast village center, draw a small floating label).
- Day/night: village window glow already exists; add campfire light halo.

**Performance targets** (llvmpipe, 5 villages × 24 pop, ~600 props ≥ 30 fps):
- `Village::step` scans every prop for corpses each frame per village
  (O(V·P)) — hoist one shared corpse pass in `World::update`, or reuse the
  obstacle-grid pattern with a coarse "bodies" bucket.
- `rebuildObstacleGrid` clears+refills every cell vector each frame —
  acceptable now; if it shows up, swap to a frame-stamped flat array.
- `GodAI::think` scans all props per owned village (stacks) and per gift —
  fine at 2.5 s cadence; don't let a future faster cadence ship without a
  grid query.
- Renderer draws every prop/villager individually — the instancing escape
  hatch is documented in plan-villagers.md; only pull it if profiling says so.

**Acceptance**: a fresh player on FAIR loses their first skirmish and wins
their third; `--match` across 10 seeds shows no god winning >70 % purely by
spawn position; steady 30+ fps on llvmpipe at the target scale; suite green.

---

## 2. Story mode (post-M8, the plan-game.md arc)

**Goal**: scripted scenarios that teach the mechanics, built ON the editor
maps — no new world machinery.

**Design — scenario files** (`scenarios/*.scn`, text, versioned):
```
map        maps/tutorial1.gmap
title      THE FIRST FLOCK
narrate    0    YOUR PEOPLE ARE HUNGRY. FEED THEM.
objective  food-stored   50     # objective kinds are a fixed enum
narrate    done PLENTY. THEY BELIEVE.
next       scenarios/tutorial2.scn
```
- Parse into `struct Scenario { mapPath, title, steps[] }` (sim-side,
  `ScenarioFile.*`, tested headless like MapFile).
- **Objective kinds v1** (each a pure predicate over `World`):
  `food-stored N`, `pop N`, `worshippers N`, `building TYPE N`,
  `belief-at VILLAGE 0.x`, `convert VILLAGE`, `rival-broken`, `survive DAYS`.
  Evaluate in App (shell-side) once per second; sim stays ignorant.
- **Narration**: the pixel font over a dimmed lower-third band; advance on
  click or timer. All-caps 5x7 is the aesthetic — keep lines short.
- **Menu**: STORY entry on the title screen listing `scenarios/` in order,
  with completion ticks persisted in `saves/story.dat` (tiny Serial blob).
- Authoring loop: build the map in the editor (M6), write the .scn by hand.
- Tests: parse round-trip, each objective predicate provable in a headless
  world, a full tutorial-1 playthrough driven by sim mutations.

**Out of scope until it hurts**: branching, timers beyond `survive`, cameras
on rails, voice.

---

## 3. Temple assault (the reserved warfare seam)

**Goal**: honor pillar 3's stub — direct temple damage as a LATE-game
alternative to pure conversion, without re-architecting.

**Design**:
- `Building` and `Temple` gain `float health` (default full). One funnel:
  `World::applyStructureDamage(target, amount, god)` — mirrors
  `villagerKill`'s discipline: no other code path may subtract health.
- Damage sources v1: thrown ROCKS above a speed threshold (the physics
  settle path already knows impact speed — route heavy impacts near a
  temple/building through the funnel), and a future Fireball miracle.
- Temple at 0 health = `collapseTemple(god, attacker)` — the ceremony
  already exists. Buildings at 0 → stage -2 ("rubble", never erased,
  builders may rebuild at wood cost).
- Defense: structures inside a god's influence regen slowly; attacking
  drains attacker belief in WITNESSING villages (terror cuts both ways —
  fear up, awe down) so conversion stays the cleaner path.
- Serialize health (SaveFile + test [15]); checksum it; headless section:
  rock siege kills a temple deterministically, regen works, belief cost.
- Balance gate: the AI does NOT get assault until a human playtest says the
  conversion war stays viable; add `kAssaultEnabled` tuning kill-switch.

---

## 4. More miracles

Follow the food-miracle template exactly: cost in Tuning.h, cast through a
`World::cast*` function that checks `insideInfluence(p, god)` and spends
`gods[god].mana`, spawn deterministic effects via `XorShift(seed_ ^
(++miracleCounter_ * K))`, credit `notifyDivineEvent(god, p, fear, awe)`,
give the AI a strategos rule, add a keybind + HUD cost hint, test headless.

Priority order (each ~a day of work):
1. **Wood miracle** (M-key cycle or `2`): rains logs; the gift economy's
   sibling; trivially reuses food's shape.
2. **Heal/Bless** at a village center: fills hunger/energy of villagers in
   radius, small awe — the defensive miracle when starvation looms.
3. **Storm** (rain): waters crops (growth burst), douses… nothing yet;
   awe/fear mix; visual = particle streaks + darkened sky tint.
4. **Fireball**: the assault miracle — waits for §3's damage funnel; fear
   heavy, kills crops in radius (set growth 0), can fell trees.

The Miracle Dispenser already stores "one chosen miracle" as charges —
generalize `Building::charges` with a `miracleKind` byte when miracle #2
lands (serialize it).

---

## 5. A third god (and N)

Everything is sized by `tune::kMaxGods` already (arrays on World, belief
arrays, rings, AI). To raise it to 3:
- `godColor(2)` (suggest teal {0.30, 0.75, 0.80}), a 3rd temple-direction
  preference, `generate(seed, 3)` picking two rival sites (farthest-pair
  greedy), menu row "OPPONENTS: 1/2".
- Ratchet already handles N challengers (best/secondB logic). Verify the
  lead-margin math with 3 belief columns in a new headless case.
- Save/map formats: belief arrays and god blocks are written per
  `tune::kMaxGods` — bumping the constant CHANGES BOTH FILE FORMATS. Bump
  `mapfile::kVersion` and `savefile::kVersion`, and either keep loaders for
  v1 (read 2, zero-fill the 3rd) or accept a clean break pre-1.0.
- AI-vs-AI: `--match` should take god count; check 3-way wars don't stall
  (two AIs courting the same neutral is the interesting case).

---

## 6. Editor v2 (deferred wishlist from plan-editor.md)

- **Village removal/undo**: rebuild-from-MapDef makes this safe — remove the
  spec, reload the snapshot. Cheapest correct version: an in-editor UNDO
  that snapshots (mapfile buffer) before every placement and pops on Ctrl+Z
  (bounded stack of ~16 buffers, ~4 MB — fine).
- **Water level per map**: store in .gmap v2; Terrain::WATER_LEVEL becomes a
  member read by everything (grep carefully — it's referenced ~30 places).
- **Starting-stores fine-tuning**: +/- keys while village tool hovers an
  existing village; writes v.food/v.wood (already serialized).
- **Map metadata**: name/author strings in .gmap v2; title menu shows names
  instead of "SLOT 2".
- **Thumbnails**: render a 128px top-down snapshot on save (offscreen FBO —
  new GL, keep it render-side) into `maps/slotN.bmp`; menu draws it as a
  quad (needs the texture path from plan-assets.md §5 first, or skip).

---

## 7. New hamlets & ruins (v2 systems, noted in plan-game.md)

- **Founding a hamlet**: placing a 5-stack Village Center on open ground
  ≥60 m from any village founds a NEW village (owner = placer) via
  `foundVillageFromSpec(site, owner, 0 pop preset)` with population 0 —
  villagers migrate: nearest owned village marks `migrants` who walk over
  (a new Job::Settler with a one-way trip, then join the new village's
  vector — the ONE place villager indices may grow cross-village; keep
  additions append-only and it stays invariant-safe).
- **Ruins**: a village whose population hits 0 keeps `founded=true` but
  gains `ruined=true`: no economy step, belief floor 0, buildings decay
  visually (render tint), reclaimable by ANY god's re-founding ceremony
  (belief > convert threshold → un-ruin, spawn preset-small pop). Serialize.

---

## 8. Sound (fully deferred so far)

- SDL2 audio directly (`SDL_OpenAudioDevice`, mix in a callback) — no new
  deps, matching the no-hard-deps rule. A tiny `Sound.*` (render-side
  sibling): load WAV blobs, N-channel mixer, distance attenuation from
  camera focus.
- Procedural placeholders first (the project's pattern): synthesized blips —
  grab/release thock, cast chime, conversion bell, collapse rumble (noise
  burst + lowpass), ambient surf tied to camera altitude. Then swap in
  extracted B&W samples behind the same `Sound::play(id, pos)` interface
  (see plan-assets.md §6).
- Events hook the existing app-side detection points (the same places
  SDL_Log fires today) — the sim NEVER calls Sound.

---

## 9. Perf & polish backlog (do opportunistically)

- Villager LOD: skip pose math + limb draws beyond ~180 m (camera distance
  check in the render loop only).
- `notifyDivineEvent` iterates every villager of every village per event —
  fine at current event rates; batch if miracles get spammy.
- Editor: throttle full terrain re-upload is 12 Hz; a dirty-rect mesh patch
  would make sculpting butter on big brushes (build only affected quads —
  Mesh partial update via glBufferSubData; new but contained).
- Save-slot metadata: store day/pop/seed in a 32-byte header so menus can
  show "SLOT 2 - DAY 7, POP 31" without full loads (bump save version).
- `--match-csv` (see M8).

## 10. State of the code: the 2026-07 audit

A multi-lens adversarial review ran after M7. Nine grounded findings, ALL
fixed and regression-locked in headless section [16]:

1. A ruined god could still claim villages through the ratchet (now gated).
2. Villagers could shoulder a prop the hand was holding (re-validated).
3. Graveyard "sustain" could reverse decay and mint unbounded belief
   (decay clamped ≥ 0, belief clamped ≤ 1).
4. Large Abode beds were phantom capacity — `findHomeFor` only knew Houses.
5. A full storage pile silently swallowed the settle-absorb transition,
   wedging trees asleep on the pad (absorb now reports decline).
6. Combining scaffolds afloat teleported the stack to the seabed.
7. Founding yaws used `std::atan2` — cross-platform bit-identity violation;
   replaced with `noise::atan2det` (fixed-order IEEE ops).
8. Deposits declined by a full store levitated at chest height and were
   instantly re-claimed (now dropped + briefly blacklisted).
9. `pickTarget`'s villager tie-break let a farther villager steal the pick.

**Audit coverage note for the next maintainer**: the review lenses that
completed were world/villagers/village-economy; the GodAI, app-shell,
save-completeness, and cross-platform/perf lenses were cut short (agent
budget) — re-run them before 1.0. Known soft spots worth a look:
`worldChecksum` does not hash prop state directly (divergence surfaces only
through villager behavior — consider adding prop pos/alive to the hash),
and the O(V·P) corpse scan in `Village::step` (see §1 perf).

## 11. Import original assets

The full plan lives in **[plan-assets.md](plan-assets.md)**: the B&W file
formats (LND terrain, G3D/L3D meshes, ANM animations, SAD audio), the
loader architecture behind the existing `Terrain`/`MeshData` interfaces, the
texture pipeline the renderer needs to grow, and the strict
personal-use/no-redistribution rules (assets load from the user's own
install at runtime; nothing copyrighted enters this repo).
