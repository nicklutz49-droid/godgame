# M1 — Scaffolds & the buildable village (slice plan)

The growth engine from docs/plan-game.md: wood → workshop → scaffold props →
combine → place → villagers build → bigger village.

## Decisions

- **Scaffolds are props** (`PropType::Scaffold`) with `resource` = stack count
  (1..7). They fall, bounce, float, and are grabbable like everything else.
- **Crafting**: the starting village layout gains a Workshop. Builders with no
  construction site to serve work the bench: each scaffold consumes 4 wood and
  ~8 s of crafting, then pops out beside the workshop. Auto-crafting stops at
  3 loose scaffolds (the village doesn't hoard); more builders = faster.
- **Combining is physical**: gently place a held scaffold onto another
  (within ~1.8 m) and they merge (sum capped at 7). Merge-only; no splitting.
- **Placing commits**: gently place a scaffold/stack on valid open ground and
  it becomes a construction site of the mapped building. While held, a ghost
  preview shows what/where (green = valid, red = won't commit - invalid
  placements just drop the scaffold loose). For 3-stacks the mouse wheel
  cycles the civic choice (Store / Workshop / Crèche / Graveyard); the wheel
  zooms as normal otherwise. No cancel in v1 - the ghost makes intent clear;
  site-cancel is a fast follow if it stings.
- **Scaffolds ARE the material**: scaffold sites need no hauled wood (the wood
  went in at the workshop); builders go straight to hammering. Build time
  scales with tier (~12 s per scaffold). The old auto-opened reserved-plot
  abodes (haul 8 wood, then build) remain as the village's autonomous slow
  fallback, per the autonomy pillar.
- **Validity**: flat enough (area-averaged), on land, clear of buildings,
  fields, the temple, and blocking props (throw the trees aside first),
  within ~55 m of the village center.

## The roster (stack count → building → effect)

| # | Building | Effect on completion |
|---|---|---|
| 1 | Small Abode | +4 beds (the existing house) |
| 2 | Large Abode | +8 beds, bigger model |
| 3 | Village Store | storage caps +80 food / +50 wood (base caps 60/30 arrive with this slice; deposits pause when full) |
| 3 | Workshop | another crafting bench (parallel scaffold production) |
| 3 | Crèche | dawn birth requirement eased (×0.6 surplus needed), children appear at the crèche |
| 3 | Graveyard | symbolic until M2 mortality: +0.015 belief/day sustain aura (then: burial) |
| 4 | Field | a new 6×4-cell farm plot at the site |
| 5 | Village Center upgrade | placed at the totem: level 2/3 - influence +8/level, worship mana ×1.15/×1.30 |
| 6 | Miracle Dispenser | mana overflowing the full pool charges it (max 3); casting within 25 m spends a charge instead of mana |
| 7 | Wonder | aura (r 45): belief decay halved, witness awe ×1.5 |

## Tests

Craft (wood falls, scaffold appears), combine (1+1=2, 3+5 refused at >7),
placement for every count incl. civic choices, tier build times, every
completion effect (beds, caps, crèche timing, field cells, center level,
dispenser charge+free cast, wonder aura math), invalid placements refused,
storage-full deposit pause, soak + determinism (buildings/fields join the
checksum).
