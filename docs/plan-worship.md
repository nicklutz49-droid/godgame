# Worship, belief, mana & the temple — design plan

Owner decisions: dedicated **Worshipper job** (drop-to-assign on the town
center totem); **full B&W influence** (belief scales worship output and the
village influence ring; the temple adds a base ring; the hand can only grab
and cast inside influence); the **central temple stands near the village**
and holds the mana pool; slice includes the **food miracle** as the mana sink
and the worship readability layer (dancing, motes, glows). No second village
yet — but all influence/worship code loops over villages, so more villages
slot in.

## The loop

worshippers dance at the totem → **mana** accrues at the temple →
the player casts **miracles** → villagers witness them → **belief** rises →
worship yields more mana and the **influence ring** grows → the hand can act
in more of the world.

## Systems

- **Worshipper job** (`Job::Worshipper`, the reserved enum slot): assigned by
  gently placing a villager within 5 m of the village center totem (highest
  drop-to-assign priority: center > site > field > tree > water). Worshippers
  walk to a slot on a ring around the totem and dance — physically circling
  it (position driven along the circle, arms-raised sway pose, prayer motes).
  While dancing they generate `kManaPerWorshipperPerDay x (0.5 + 1.5 x belief)`
  and their hunger/energy drain 1.6x/1.8x faster, so they eat from storage and
  sleep like everyone else — every worshipper is a worker you gave up, who
  must still be fed. Worship also sustains belief (+0.06/day per dancer).

- **Belief** (`Village::belief`, 0..1, starts 0.25, decays 0.08/day toward a
  0.12 floor): raised by witnessed divine acts through the existing
  `notifyDivineEvent` bus, which now carries fear AND awe. Witness counting is
  proportional: belief moves by `awe x witnesses / population`. Sources:
  grabs (tiny), throws (small, plus fear), hand-gifted resources placed on
  storage (medium, no fear), miracles (large). Effects: worship multiplier
  and village influence radius `50 + 90 x belief` (50..140 m).

- **The temple** (`World::temple`): founded right after the village on the
  first workable direction ~48-64 m out (field direction excluded), on its own
  flattened terrace, facing the village. Holds the mana pool
  (`mana`/`manaMax`, starts 20/100) — the beacon crystal's glow tracks the
  fill fraction. Projects a fixed 70 m base influence ring. Placeholder model:
  stepped stone platform, columns, roof slabs, gold crystal.

- **Influence enforcement**: `World::insideInfluence(p)` = inside the temple
  ring or any village ring. Outside it the hand cannot hover-highlight, grab,
  or cast — it renders ghostly (grey tint, faded) — but it can still pan the
  camera, carry things out, and throw things anywhere from inside. Rings are
  drawn as terrain-following gold bands (rebuilt only when a radius changes).

- **Food miracle** (`World::castFoodMiracle(p)`, key `M` at the cursor):
  costs 30 mana, requires the point inside influence. Four food bundles rain
  from the sky with deterministic scatter (per-cast XorShift stream), landing
  as normal Food props — on the storage pad they absorb, in the field
  villagers haul them (farmers now treat loose food like foresters treat
  loose logs). Witnesses gain strong awe + a little fear. Gold ring-pulse
  effect at the cast point.

## Mortality/future seams preserved

Worship slots into the job system, not a new ladder rung — the reserved rung
comment stays for future *scheduled communal* worship. Multi-village support:
`insideInfluence`/witness code takes the village list. Belief will later feed
the dawn growth multiplier (`Village::step` comment marks the spot).

## Tests (extend the headless suite)

- resolve at the totem -> Worshipper; gentle placement path assigns it.
- A worshipper dancing for a day generates mana; belief rises above decay.
- `insideInfluence` true at the village, false at the far shore.
- Miracle: fails outside influence and with insufficient mana; succeeds
  inside (mana falls, >= 4 food props exist, belief jumps).
- Soak: a starter Worshipper replaces one idle villager; assert mana grew
  over 3 days alongside the existing economy asserts.
- Determinism checksum extended with mana + belief.
