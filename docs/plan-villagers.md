# Villager simulation — design plan

Synthesized from three design passes (B&W-fidelity, simulation-architecture,
scope/milestones lenses) evaluated against the actual codebase. Locked scope
from the project owner:

1. **Living village core** — one village (center, houses, storage), needs
   (hunger, energy), jobs (forester, farmer, fisherman, builder), real
   resource flow, modest population growth. Worship/belief deferred.
2. **Full B&W hand interactions** — grab/throw villagers (flail, panic),
   drop-to-assign jobs, drop resources into storage, villagers react to the
   hand.
3. **Day/night cycle** — sun arcs, lighting shifts, villagers sleep at night.
4. **Invulnerable villagers** this slice — hard impacts stun, never kill, but
   authentic B&W mortality must plug in later at explicit seams.

## Core architectural decisions

- **Villagers are a parallel entity, not a `Prop`.** A villager is an agent
  (kinematic walker, brain, needs) that only behaves like a rigid body while
  thrown. The shared 40 lines of ballistic math (gravity → integrate → tumble
  → sphere-vs-heightfield bounce) are extracted from `World::update` into
  `Physics.h` (`stepBallisticSphere`) and used by both props and airborne
  villagers. The extraction is regression-checked against the existing
  headless rock-throw numbers before anything else lands.
- **Resources are Props.** New `PropType::Log / Food / Stump`. Logs and food
  bundles fall, float, and are grabbable/throwable by the hand with zero new
  hand code. Trees gain `resource` (wood remaining); chopped trees fell with
  full physics drama (no replant, via a `felled` flag), then become stumps and
  spawn logs. Props are never erased mid-session: `alive=false` slots are
  free-listed by `World::spawnProp`, so indices never dangle.
- **Anything at rest inside the storage radius is absorbed.** One rule handles
  villager hauling, hand placements, dunked whole trees (wood ∝ scale), and
  lucky cross-map throws.
- **AI = fixed priority ladder + per-job flat FSMs**, re-decided on a
  staggered 0.4 s think tick. No behavior trees, no utility framework.
  Ladder: physical (held/airborne/stunned/swim) > panic/cower > sleep
  schedule > critical hunger > *(reserved rung: worship)* > job loop > idle.
  Hysteresis on every threshold. All transitions out of Work release claims.
- **Steering, no A\*.** Probe-ahead slope/water/obstacle deflection + stuck
  watchdog (abandon task, re-plan, never teleport). The village terrace is
  flattened; work targets are validated reachable at plan time.
  `planPath()` seam exists for a coarse A* later if soaks show abandonment.
- **Determinism**: per-villager XorShift streams (`seed ^ hash(index)`),
  index-ordered thinks, no libm in decision paths (schedule uses smoothstep of
  clock `t`; `sin/cos` only in render-side sun direction), needs advance in
  day-fraction units so tests can shrink the day. Double-run FNV checksum in
  headless.

## Systems

### Villager

`src/Villager.h` — plain data: pos/vel/yaw, `Job`, `VState`
(Idle/Wander/Chat, GoTo/Work/Haul/Eat, GoHome/Sleep, Held/Airborne/Swim,
Stunned/GetUp/Panic/Cower), hunger/energy/fear, carried prop, target prop /
building / farm cell, home index, growth (child 0.55 → adult 1.0), per-villager
rng, walk phase. AoS `std::vector<Villager>` owned by `Village` (owned by
`World`). Never removed this slice → stable indices.

### Jobs (minute-to-minute)

- **Forester**: claim nearest live tree in work radius → walk → chop (8
  anticipation-snap swings, tree shudders per strike) → tree fells with
  physics, becomes stump, spawns 2 logs → shoulder log → haul → deposit.
  Loose logs outrank standing trees (self-cleanup).
- **Farmer**: 24-cell field; tend least-grown cell (hoe cycle, growth 3×
  while tended), harvest ripe cell → Food bundle → storage.
- **Fisherman**: precomputed shore spots; cast/wait loop, seeded catch
  chance → Food → storage.
- **Builder**: construction site needs wood delivered (1 log/trip from
  storage), then hammer through 3 visible build stages. One site is open from
  the start; new sites open at pop ≥ 0.8 × capacity and wood ≥ cost.
- Interrupts: grab instantly (cargo drops as a live prop); night finishes the
  atomic action first; urgent hunger drops cargo. Claims released on every
  Work exit; targets re-validated each think (tree stolen by the hand → stop,
  shrug, re-plan).

### Economy

Integer wood (1 = one log) and food (1 = one meal) on `Village`. Tree = 2
logs; house = 8 wood; farm harvest = 4 meals; fish = 3 meals; eat = 1 meal at
the storage pile (visible lunch queues). Hunger/energy scale to day length.
Starvation clamps hunger at 1.0 → listless slump (the failure state reads
without death). Dawn tick: if food/pop > 1.5 and housing spare and pop < 24,
an occupied house spawns a **child** (scale 0.55, grows over 2 days, follows
adults, assignable like anyone).

### Village layout (deterministic, runs between terrain gen and prop scatter)

Coarse-grid site scoring: flatness · height band [2.5, 12] · coast within
70 m · tree density. Three-pass hostile-island policy: strict → relaxed →
terraform-harder (never regenerate the seed). `Terrain::flattenDisc` (r≈30,
smoothstep falloff) runs before mesh build so `heightAt`/raycast/mesh agree
automatically. Ring layout: center totem (**the reserved worship anchor**),
storage pad, campfire, 3 built houses + 1 open site + reserved plots, field
on the flattest adjacent rectangle, 2–3 fishing spots. Prop scatter rejects
the footprint. Spawn 8: one of each job + 4 jobless (the player's clay).

### Day/night

`src/DayCycle.h` (pure, sim-side): `t ∈ [0,1)`, 360 s/day (tests shrink it),
`daylight()` piecewise-smoothstep for the sim schedule, `sunDir/sunColor/
ambient/fogColor` curves for the renderer. Dusk: finish atomic action → walk
home → slide into the house (hidden, energy refills); homeless lie around the
campfire. Dawn: staggered emergence + stretch. Lit shader gains `uSunColor`,
`uAmbient`, `uTint` (job tint / flashes); sky gains night palette + hashed
stars; campfire cone + window-glow quads are emissive at night.

### Hand ↔ villagers

`GrabTarget {kind: Prop|Villager, index}` replaces `Hand`'s raw prop index;
nearest sphere hit wins, villagers get a tie bonus. Grab: forced Held, cargo
drops, claims release, panicked wriggle (per-villager phase), nearby
witnesses gain fear (**the future belief-witness bus**). Release at smoothed
gesture speed ≥ 7 m/s → throw (ballistic, flailing, tumbling); < 7 → place:
land on feet + **drop-to-assign**: construction site (6 m) > field (+2 m
margin) > tree (4 m) > water/shore → job flips with a tint flash and the
villager walks straight to work; no match → shrug. Thrown villagers are never
assigned. Storage placement of Log/Food/Tree absorbs with a shrink flourish.
Ambient awareness: head-track within a reach radius scaled by hand altitude;
fast low swoops cause flinch/cower; landing in water → Swim ashore
(`swimStamina` maintained but non-lethal this slice).

### Rendering (all procedural placeholder)

Six-part villager (head, torso, 2 arms, 2 legs from box/cylinder primitives,
~2 m tall) with **procedural pose math** (`computePose`, pure function:
walk swing + bob, chop anticipation-snap, hoe/hammer/cast, flail with
per-limb phases, stun lying, cower) — parts drawn with `uModel` + `uTint`
(job color). Far LOD: torso+head only. Thought bubbles (hunger / sleep /
fear) as tiny emissive billboard meshes shown at need thresholds. Buildings:
staged house meshes, tiered storage piles, totem, campfire + rising smoke
discs, field slab + growth-scaled crop cones. Blob shadows reuse the existing
disc pass. Budget: ≤ 24 villagers × ≤ 6 draws + buildings ≈ well under the
existing ~450 prop draws.

## Mortality seams (invulnerable now, authentic later)

- `applyLanding(v, impactSpeed)` is the **single funnel** for every hard
  contact: today stun-clamps; later `impactSpeed > kLethal → kill()`.
- `kill(v, cause)` + `Village::onVillagerDeath` compiled stubs from day one.
- Swim tracks `submergedTime`/`swimStamina` — drowning is one threshold later.
- Starvation is a one-line clamp in `updateNeeds` — death swaps the clamp.
- Bodies = future `PropType` value; graves = future building kind (grabbable
  corpses come free through the prop system). Headless asserts survival at
  200 m/s — the test that flips by design when mortality lands.

## Worship/belief seams

Center totem placed from day one; reserved ladder rung between hunger and job;
`Village::notifyDivineEvent` (built for fear) is the witness bus; dawn growth
takes the future happiness/belief scalar; influence ring = f(center, belief).

## Testing

- Physics-extraction regression: existing rock numbers must not change.
- Layout asserts + relaxation-pass histogram over 20 seeds.
- 3-sim-day soak (60 s days, fixed dt): wood/food produced *and* consumed,
  ≥ 1 house completed, pop grew, positions finite/in-bounds, nobody pinned at
  hunger 1.0, ≥ 80 % asleep at midnight, ≥ 60 % active at noon, stuck
  watchdog under threshold.
- Scripted hand: throw 30 m/s → Airborne→Stunned→GetUp→Panic→resume; 200 m/s
  → alive (invulnerability tripwire); gentle drops on tree/field/water/site
  flip jobs per priority; log placed on storage increments wood.
- Determinism: two worlds, same seed, 2 000 steps → equal FNV checksums.
- Screenshot views: `village` (noon) and `night`, plus optional time-of-day
  argument.

## Milestones

| # | Lands | Verified by |
|---|---|---|
| M1 | Physics extraction; Tuning.h; DayCycle + day/night rendering | rock regression; night screenshot darker |
| M2 | Village founding: site selection, flattenDisc, buildings, keep-out | layout asserts + seed histogram; village screenshot |
| M3 | Villagers walk & live idly (steering, wander/chat, pose rendering) | wander soak: finite, on land, deterministic |
| M4 | Hand meets villagers: grab/throw/flail/stun/panic, drop-to-assign | scripted-hand suite; manual feel pass |
| M5 | Jobs & economy: forester/farmer/fisherman, hunger/eat, absorption | 3-day economy soak |
| M6 | Builder, growth/children, sleep schedule, night village life | full soak + night screenshot with fire/window glow |

## Cut list (this slice)

Multiple villages; A*/navmesh; villager-villager collision (soft separation
only); names/genders/ages; any text UI (window title + SDL_Log census only);
sound; tree regrowth; multi-item inventories; grabbable houses; weather;
save/load; instancing; miracles.

## Tuning workflow

`src/Tuning.h` holds every gameplay constant. Debug keys: F4 sim speed
×1/4/16, T scrub time-of-day, K spawn villager at the hand, L +10 wood/food,
F3 villager state-tint. One SDL_Log census line per sim-day; window title
shows pop/wood/food.
