# Slice plan: the map editor (M6)

Handcrafted worlds. The god-hand becomes an author's hand: sculpt the island,
plant its forests, seat its villages and temples, and save the result as a
map file that a skirmish can load. Master arc: [plan-game.md](plan-game.md).

## Locked decisions

- **Entry: both.** `Tab` toggles editor/play instantly in any session (the
  sandbox toy); `--editor` boots straight into authoring.
- **Canvas: both.** Sculpt the current procedural island (`R` still rerolls),
  or `N` for a blank flat starter island.
- **Sim: frozen.** Time stops in the editor. Toggling either way rebuilds the
  world from the map, so playtests never dirty the map and every playtest is
  a fresh deterministic start.
- **Files: maps/ folder + slots.** `F6` cycles slots 1-4, `F5` saves
  `maps/slot<N>.gmap`, `F9` loads it. `--map <file>` plays (or, with
  `--editor`, edits) any map file.

## The architecture in one paragraph

A map is a **starting condition**, not a save-game (full-state saves are
M7's). `.gmap` v1 = version + seed + the raw heightfield + entity *specs*:
trees/rocks (pos, variant, scale, yaw), villages (site, owner, size preset),
temples (god, pos, yaw). Loading rebuilds the world through the exact
founding path the generator uses — `Village::plan` + `spawnVillagers` per
spec, gods activated by what the map contains — so loaded worlds obey every
existing invariant (stable indices, determinism, checksums) for free. Saving
*derives* the specs from the live world; save → load → save is byte-stable.
All of it lives in sim-side `MapFile.*` (no GL) and is headless-tested.

## Tools (number keys; `[` `]` brush radius; title bar shows the tool)

1. **Raise** / 2. **Lower** — smooth-falloff dome up/down.
3. **Flatten** — terrace toward the height sampled where the stroke began.
4. **Smooth** — relax toward the neighborhood average.
5. **Forest** / 6. **Rocks** — plant trees/boulders (collision-checked,
   deterministic per stroke).
7. **Erase** — remove trees/rocks/stumps under the brush.
8. **Village** — click to found (needs land + 60 m separation; ghost totem
   shows validity). `G` cycles owner You/Rival/Neutral, `V` cycles size
   Small/Medium/Large (population and starting stores).
9. **Temple** — click to seat a god's temple (`G` picks the god); it faces
   the nearest village its god owns.

Terrain edits re-snap nearby props, buildings, and villagers; the terrain
mesh re-uploads on a short throttle while sculpting. Villages still terrace
their own ground when founded, exactly as in generated worlds.

## Rules that keep it honest

- Editor mutations go through World editor verbs (`editorPlaceVillage`,
  `editorPlaceTemple`, `editorPaintForest`, ...) — deterministic, sim-side,
  reusing the founding code. No new spawn paths.
- A map with a rival-owned village (or rival temple) wakes `gods[1]` with its
  AI on load — skirmish maps are just maps that contain an opponent.
- Removing a placed village is v2 (reload the slot to revert); erasing
  props uses the standard `alive=false` slot reuse.
- The procedural generator remains the random-map path; the editor never
  forks world-building logic.

## Tests (headless [14])

- Blank-island generation: flat build plateau, beach ring, deterministic.
- Brush ops: bounded, deterministic, min/max stats stay correct.
- Editor verbs: presets spawn the right population/stores; owners seed the
  right belief; a rival village activates the rival god.
- Round-trip: build a skirmish world → save to buffer → load into two
  worlds → identical checksums; run both 100 steps → still identical;
  save → load → save is byte-identical.
- A loaded map plays: villagers work, worship fills mana, ownership rules
  hold.

## Out of scope (v2 / later milestones)

Village removal/undo, water level editing, starting-store fine-tuning beyond
presets, map metadata (name/author), menu-driven map browsing (M7 shell),
scenario scripting (story arc).
