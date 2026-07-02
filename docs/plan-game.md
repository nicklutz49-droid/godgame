# godgame — master plan: from sandbox to skirmish

The design + implementation arc from the current build (one autonomous
village, worship/mana/belief, food miracle, influence rings) to the full
game. Owner decisions locked in this document; each milestone below becomes
its own detailed slice plan when it starts (like plan-villagers.md /
plan-worship.md).

## Vision

**Skirmish**: the player god against enemy AI gods on handcrafted maps.
Start with a temple and one small village among several neutral villages.
Grow your village (economy → scaffolds → buildings → population → worshippers
→ mana → influence), court nearby neutral villages with miracles until they
convert, push your borders into enemy territory, and win by converting every
enemy village — their unsupported temple collapses. **Story mode comes after
skirmish is fun**: scripted scenarios that teach the same mechanics.

## Design pillars

1. **Villages run themselves; gods direct.** Every village — owned, neutral,
   enemy — survives autonomously: gathers, eats, builds slowly, raises
   children. A god's hand *accelerates* and *directs* (job assignment,
   scaffold placement, miracles, gifted resources) and covers deficiencies.
   The player and the AI god use exactly the same verbs.
2. **Everything is physical.** Scaffolds, bodies, resources, food from the
   sky — all props the hand can grab. UI stays diegetic wherever possible
   (piles show stock, beacons show mana, rings show reach).
3. **Belief is territory.** Influence rings are the map's borders; conversion
   is the war. Warfare is **conversion-only for now** — but temples get
   health fields and a damage funnel *stub* so direct temple assault can
   arrive in a later build without re-architecture.
4. **Deterministic and headless-testable forever.** Every system lands with
   self-tests; AI-vs-AI headless matches become the balance tool.

## The systems

### Ownership & conversion (the ratchet)

- `God { id, temple, mana, color, isPlayer }`; `World` owns gods + villages.
  Villages have `owner` (a god or Neutral) and **belief per god**.
- All divine acts carry the acting god through the witness bus
  (`notifyDivineEvent(god, where, fear, awe)`); worship feeds the owner only.
- **Ratchet with resistance**: a neutral village converts when a god's belief
  crosses ~0.5 (and leads any rival clearly). An **owned** village can be
  stolen, but the challenger must overwhelm: belief above ~0.85 while the
  owner's has decayed below a defense floor. Flips are ceremonies: totem
  repaints, tunics re-accent, worship redirects, a few frightened villagers
  scatter.
- Influence = union of your temple ring + your villages' belief-scaled rings.
  You can only act inside your own union (as today).

### Scaffolds & building (the growth engine)

- **Workshop** buildings turn stored wood into **scaffold props** (a crafter
  works the bench; scaffolds pop out physical and grabbable).
- **Combining**: drop a scaffold onto another and they merge into a stack
  (2..7, visibly taller). The stack count chooses the building:

  | Scaffolds | Building |
  |---|---|
  | 1 | Small Abode |
  | 2 | Large Abode |
  | 3 | Civic: Village Store, Workshop, Crèche, or Graveyard |
  | 4 | Field |
  | 5 | Village Center |
  | 6 | Miracle Dispenser |
  | 7 | Wonder |

- **Placement**: gently place a stack on valid ground → construction site;
  builders haul remaining wood and raise it through the existing staged
  construction. While holding a 3-stack the mouse wheel cycles a ghost
  preview of the four civic buildings (wheel returns to zoom otherwise).
- **Building effects**: Store raises storage caps (stores gain caps at all);
  Crèche raises birth rate and hosts children; Graveyard enables burial
  (below); Field as today but player-placed; Village Center upgrade raises
  worship-ring capacity (placing one far from any village to found a *new*
  hamlet is a v2 feature, noted not promised); Miracle Dispenser is charged
  by worship and stores free casts of one chosen miracle; Wonder projects an
  awe aura (slower belief decay, amplified witness awe).
- Autonomous villages still slowly self-build small abodes (autonomy pillar);
  player scaffolding simply outruns it. The current auto-opened construction
  site becomes that fallback path.

### Mortality & burial (arrives with the Graveyard)

- Flip the prepared seams: lethal impacts in `applyLanding`, drowning via
  `submergedTime`, starvation replacing the hunger clamp. The headless
  invulnerability test flips into a lethality test, as designed.
- Death produces a **Body prop** (grabbable — the god can fling corpses, of
  course). Villagers carry bodies to the Graveyard for burial: graves grant
  the owner belief (dignity); corpses left out drain belief and frighten.
- Population becomes a real resource: Crèche births vs. deaths. A village
  that loses everyone becomes a ruin (neutral, reclaimable later).

### The rival god (fully symmetric)

- The AI god acts **only through the player's verbs**, executed by an
  embodied enemy hand (rendered as a ghostly rival-colored hand when it acts
  where you can see — readable and menacing) with rate limits so it cannot
  out-click humanity.
- Three layers: **governor** per owned village (job mix, scaffold queue,
  feeding), **strategos** (which neutral to court, where to spend mana, when
  to contest your villages), **executor** (the hand: grabs, placements,
  casts, all inside its own influence).
- No economic cheats; difficulty = decision cadence, mana efficiency, and
  aggression profile. Balance tool: deterministic AI-vs-AI headless matches.

### Maps: in-game editor

- Editor mode: the god-hand sculpts — raise/lower/flatten/smooth terrain
  brushes, forest and rock brushes, village placement (size presets, owner),
  temple placement per god, starting stores. Save/load to a versioned map
  file (raw heightfield + entity list). The procedural island remains the
  random-map generator.
- The editor doubles as the sandbox toy and is the authoring tool for the
  eventual story scenarios.

### Shell, modes, persistence

- Minimal main menu (the island as backdrop): Sandbox (today's game),
  Skirmish (map + opponent count), Editor. Story later.
- Save/load of full game state (versioned binary; the determinism discipline
  makes state serialization the honest approach — no replay tricks).
- Win/lose: last enemy village converts → their temple crumbles (prop
  physics celebration); yours falls the same way. Victory/defeat card.
- First real HUD: a tiny built-in bitmap font (procedural glyph quads, no
  textures) for mana/population/objective readouts — still minimal, still
  mostly diegetic.

## Technical evolution required

- **Multi-village World**: `world.village` → `std::vector<Village>`; witness
  routing, influence unions, per-village autonomy everywhere. The largest
  refactor; do it before gods/conversion.
- **Spatial grid** for villagers/props once 3-5 villages × 24+ pop exist
  (separation and prop scans are O(N²)/O(N·P) today).
- **God abstraction** above Village/Temple; mana moves onto God.
- **Damage seams** (future direct assault): buildings/temples get `health`
  fields + one `applyStructureDamage` funnel, unreachable in conversion-only
  play — mirroring how mortality waited behind `applyLanding`.
- **Serialization** of World; map file IO; menus/mode state machine.
- Rendering: instancing escape hatch if draw counts demand it (documented in
  plan-villagers.md); enemy-hand rendering; font quads.

## Milestone arc (each lands as a tested, playable slice)

| # | Slice | Proves |
|---|---|---|
| M1 | **Scaffolds & the buildable village**: workshop + crafter, scaffold props, combining, placement (incl. civic wheel-pick), Store/Crèche/Graveyard(symbolic)/Field/Center-upgrade/Dispenser/Wonder effects, storage caps | The growth engine: wood → scaffolds → buildings → bigger village |
| M2 | **Mortality & burial**: flip the seams, Body props, burial tasks, graveyard belief loop, starvation/drowning/impact deaths | Population as a resource; the Graveyard earns its place |
| M3 | **Many villages, one god**: multi-village refactor, neutral villages on procedural maps, witness routing, influence union, spatial grid | The strategic map exists |
| M4 | **Gods & conversion**: God abstraction, per-god belief, the ratchet, ownership flips, per-god mana | You can win over a village |
| M5 | **The rival god**: governor/strategos/executor AI, enemy hand embodiment, difficulty knobs, AI-vs-AI headless matches | A skirmish can be lost |
| M6 | **Map editor & map files**: sculpt/paint brushes, entity placement, save/load maps | Handcrafted worlds |
| M7 | **Skirmish shell**: menus, map select, game save/load, win/lose flow, HUD font | It's a game you launch and finish |
| M8 | **Balance & feel pass**: economy/conversion tuning at scale, perf, juice | It's fun on purpose |
| — | **Story mode arc** (post-skirmish): scenario scripting on editor maps, objective triggers, narration | Onboarding & drama |

Sequencing rationale: M1-M2 deepen the solo game immediately using systems
that already exist (construction, props, belief); M3 is the big refactor done
before anything depends on gods; M4-M5 turn it into a war; M6-M7 wrap it in a
product; story rides on all of it.

## Risks

1. **Multi-village performance** — grid + LOD land in M3 before scale does.
2. **AI god fun/fairness** — verbs-only architecture keeps it honest;
   deterministic AI-vs-AI soaks make tuning observable.
3. **Editor scope creep** — fixed brush set (4 terrain, 2 vegetation), fixed
   entity set; anything else is v2.
4. **Civic-pick UI feel** (wheel-cycle while holding) — prototype early in
   M1; fallback is place-then-tap-to-cycle.
5. **Save/load vs determinism drift** — serialize complete state; never
   depend on replay.
