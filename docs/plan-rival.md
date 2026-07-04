# Slice plan: the rival god (M5)

The island gets a second god. It plays the same game the player plays — same
verbs, same funnels, same physics — executed by an embodied enemy hand that
must fly to a thing before it can touch it. When it works, a skirmish can be
lost. Master arc: [plan-game.md](plan-game.md).

## Non-negotiables (from the master plan)

- **Fully symmetric, verbs only.** The AI touches the world exclusively
  through the funnels the player uses: grab/carry/release (props and
  villagers), drop-to-assign, `tryPlaceScaffold` / `tryCombineScaffold`,
  `castFoodMiracle(p, god)`, gift attribution via `thrownByGod`. No direct
  resource edits, no teleports, no belief pokes. Its villages run the same
  autonomous simulation the player's do.
- **Embodied.** One enemy hand with a travel speed and an action cooldown; it
  cannot out-click humanity. Rendered as a ghostly rival-colored hand —
  readable and menacing. Villagers fear it like they fear yours.
- **Deterministic.** The AI owns one XorShift stream, thinks on a fixed
  cadence, acts in god-index order inside `World::update`. AI-vs-AI matches
  checksum identically run-to-run.

## World generation (skirmish worlds)

- `World::generate(seed, godCount)` — godCount 2 founds
  `2 + tune::kNeutralVillages` sites. `villages[0]` stays the player's home
  (best site); the **rival home** is the picked site farthest from it
  (deterministic; if a hostile island yields fewer than 2 sites the rival
  simply never activates). Remaining sites stay neutral.
- The rival home founds exactly like the player's: same starter job mix
  (including a Worshipper), `belief[1] = kBeliefStart`, a second temple
  founded just outside it (`foundTemple` generalized per god), rival mana at
  `kManaStart`. Prop scatter keeps clear of every founded temple.
- The interactive game generates skirmish worlds by default (`--no-rival`
  reverts to the peaceful sandbox). Headless tests keep single-god worlds
  except the new rival section.

## The three layers

**Governor** (per owned village, keeps the engine running):
1. *Devote*: keep `1 + population / kAiWorshippersPer` worshippers dancing —
   grab an idle adult, drop it on the totem (drop-to-assign does the rest).
2. *Feed*: village food below `kAiFoodReserve` and mana affords → food
   miracle over the storage pad.
3. *Build*: pick a target from needs — beds tight → 1-stack abode; fields
   thin → 4-stack; stores brimming → Store; deaths unburied → Graveyard;
   grown village → Crèche; reach wanted → 5-stack Center upgrade; then
   Dispenser, then Wonder. Execute with the hand: combine workshop-yard
   stacks toward the target count (never wasting past 7), then place on a
   validity-checked ring around the village (Center upgrades place at the
   totem).

**Strategos** (the war): pick the nearest neutral to any owned village as the
courting target. Inside influence with mana to spare → miracle at their
center. Out of reach → **gift run**: grab a loose food/log inside own
influence, fly toward their village, and hurl it onto their storage pad
(ballistic, `thrownByGod` credits the landing). With no neutrals left — or
mana-rich and feeling bold (`kAiAggression`) — gift-bomb the player's
weakest village to build steal pressure. Miracles into player land become
possible only where rings overlap, exactly as for the player.

**Executor** (the hand): one order at a time — fly to the pickup, grab, fly
to the target, release (gentle place, drop-to-assign, combine, place, throw,
or cast). Re-validates its target every phase (the player may have snatched
it). `kAiHandSpeed` m/s travel, `kAiActCooldown` between orders,
`kAiThinkPeriod` between decisions: difficulty knobs, not cheats.

## Losing (and winning)

A god with no owned villages is **broken**: its AI goes still (the hand
retreats to its temple) and its worship income is gone; the temple's collapse
ceremony is M7's. `World::godBroken(g)` reports it; the app announces defeat
(the player's last village fell) or victory (the rival is broken). The
conversion ratchet from M4 is unchanged — this slice just adds the opponent
pushing on it.

## Tests (headless [13])

- Skirmish start is symmetric: one rival-owned village far from home, second
  temple founded and apart, rival Worshipper exists, rival belief seeded.
- Rival worship fills `gods[1].mana` (never the player's pool).
- The hand acts: strip the rival home of worshippers → the AI devotes an
  idle adult (grab → carry → drop on totem → drop-to-assign).
- Feeding: starve the rival store with mana banked → the AI casts food over
  its own pad.
- Courting: a long soak shows gift runs / casts happening and the target
  neutral's belief in the rival rising.
- Broken gods: convert the rival's last village away → its AI goes still;
  convert the player's away → `godBroken(0)` (a skirmish can be lost).
- AI-vs-AI determinism: two identical runs with both gods AI-driven produce
  identical checksums (the checksum now folds in the AI hands).

## Out of scope (later milestones)

Temple collapse ceremony & win/lose flow (M7), difficulty presets in a menu
(M7), AI use of future miracles (with those miracles), any direct
temple-attack warfare (reserved seam), map-editor authored starts (M6).
