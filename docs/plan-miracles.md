# godgame — the miracle book (M11)

Food has company: RAIN, FOREST, FIREBALL. Decisions locked with the owner:
number keys 1-4 select, M casts at the cursor (HUD shows name/cost/state);
Rain & Forest unlock with a completed Miracle Dispenser, Fireball with a
completed Wonder; the rival rains at every difficulty but throws fire only
on CRUEL. All three respect the ratified funnels — influence-gated,
mana-spending, awe/fear through `notifyDivineEvent`, no new kill paths.

## 1. The spells

| # | miracle  | cost | gate       | effect |
|---|----------|-----:|------------|--------|
| 1 | FOOD     |   30 | always     | (unchanged) bundles from the sky; dispenser charges still pay first |
| 2 | RAIN     |   40 | Dispenser  | a cloud (r=16) for ~45 s: crops under it grow at kRainGrowthBoost even in the dark; gentle awe at cast |
| 3 | FOREST   |   50 | Dispenser  | up to 7 trees sprout across r=14 (land only, clear of buildings and each other); awe at cast |
| 4 | FIREBALL |   70 | Wonder     | a comet falls on the point: everything loose in r=9 is FLUNG (villager deaths only via the existing landing seam), trees scorch to stumps, big fear + a sliver of awe |

Gates read `Village::countCompleted` over villages owned by the caster —
build the thing, earn the power. Mana max stays 100, so Fireball is most
of a full pool: smiting is a choice, not a habit.

## 2. Sim state & funnels

- `World::castMiracle(kind, p, god)` is the one entry (M = the player,
  GodAI = the rival). Shared preamble: active god, founded temple,
  `insideInfluence`, `miracleUnlocked`; then per-kind cost + act +
  `notifyDivineEvent`. `castFoodMiracle` remains as the food path
  (dispenser-charge discount is food-only for now).
- `World::rains` (`pos/radius/age/duration/god`) and `World::fireballs`
  (`pos/vel/god/age`) are NEW SIM STATE: both ride `worldChecksum` and the
  save file (v3), both update deterministically inside `World::update`
  (fireballs integrate the shared gravity; rain just ages out).
- Crops: `Village::step` asks `world.rainIntensityAt(cell)` — wet cells use
  `max(sun, 0.85)` for light and multiply rate by kRainGrowthBoost.
- Scorch reuses the fell-to-stump slot mutation (no logs, no wood — fire
  gives nothing back).
- **Events seam** (also the M12 sound hook): `World::events` is a small
  frame-transient list (`kind/pos/magnitude/god`) appended by casts,
  impacts, blooms; the APP drains it after every update (a sim-side size
  cap keeps undrained headless runs bounded). It is render/audio DATA
  ONLY — never a sim input, never in the checksum or saves.

## 3. The rival learns weather (and, on CRUEL, wrath)

- New verbs `Rain` (all profiles: own village, fields thirsty, unlocked,
  mana spare → hover to the fields and cast) and `Smite` (CRUEL only:
  unlocked via its own Wonder, mana-rich, aggression roll → the enemy's
  weakest village center, and only if the ring genuinely reaches it).
- The governor already plans Dispensers (pop ≥ 14) and Wonders (pop ≥ 18)
  at every difficulty, so the gates are real for the rival too; EASY/FAIR
  simply never USE the fireball — difficulty stays cadence/appetite/
  boldness, still no cheats.
- Pacing guard: `--match` sweep after landing; rain mostly accelerates the
  rival's own economy (safe), Smite is late-game by construction.

## 4. UI & render

- `1-4` select (play mode only; editor keeps its tools), `M` casts.
  HUD line: `M: RAIN 40` plus `LOCKED (DISPENSER)` / `NEED MANA` states.
- Rain draws as an emissive dark disc overhead + two offset copies of a
  streak mesh falling in a loop; fireballs are emissive comets that join
  the night point-light gather; explosions reuse dust + shake, forest
  casts a green pulse. No new shaders.

## 5. Tests ([19])

Gating (locked → refused, build dispenser → allowed), mana refusal,
influence refusal, rain boosts a tended cell measurably vs a dry twin
world, forest plants only valid ground deterministically, fireball flings
a villager + scorches a tree + spends mana, save/load equality with a
cloud AND a comet in flight (v3), FAIR-never/CRUEL-eventually smite
policy, and the two-run checksum stays bit-for-bit.

## 6. Shipped (M11) — sweep verdict

All of the above landed; suite at 255 checks. Post-change pacing sweep
(FAIR vs FAIR, real day length, seeds 1-8): firstConv = 4, 2, 5, 4, -1,
4, 4, 2 — seven of eight in the deliberate band. Seed 5 turned long-siege
(converts day 22; both gods rain, both economies hold): tolerated as
variance, not a stall — contested ground remains and the war resolves.
Rival counters at day 25 on seed 5: rainCasts flowing once its dispenser
stands; smites stay 0 below CRUEL by policy.
