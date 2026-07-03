# Slice plan: the skirmish shell (M7)

It becomes a game you launch and finish: a menu in front of a living island,
full game saves, a war that ends in a collapsing temple and a card that says
so, and the first readable HUD. Master arc: [plan-game.md](plan-game.md).

## Locked decisions

- **Game saves: quick slots + menu.** In play, `F5`/`F9` save/load
  `saves/slot<N>.sav` (`F6` cycles, mirroring the editor's map slots); the
  title menu's CONTINUE resumes the newest save; `--load <file>` from the CLI.
- **One rival for now.** The menu picks map + opponent (skirmish/sandbox);
  `kMaxGods` stays 2. A third god is a later slice — everything is already
  sized by the constant.
- **After the card, keep playing.** Victory/defeat is announced (and the
  loser's temple physically collapses), but time keeps flowing; Esc offers
  the way out.
- **Esc = pause menu.** The game boots to a title menu over a live island;
  Esc pauses into the same menu shell. Quitting is a menu act.

## The pieces

**Font (`src/Font.*`)** — a built-in 5x7 pixel font: glyphs append flat
quads to a `MeshData` (one per lit pixel), drawn by the existing lit shader
with `uEmissive = 1`, fog off, through a pixel ortho projection. No textures,
no new shaders, headless-testable geometry.

**HUD** — one line of the god's ledger (mana, population, wood, food,
belief, day) plus a war line when a rival is awake (villages held: yours /
theirs / free). The victory/defeat card is centered text over a dimming
band. Menus reuse the same text pass.

**Day counter** — `DayCycle` counts wrapped days; the HUD shows "DAY N";
saves carry it.

**Temple collapse (sim)** — when a god's last village converts away,
`World::updateOwnership` fires `collapseTemple`: the temple un-founds
(`God::ruined`), a deterministic burst of rubble rocks is flung outward
(ordinary props - grabbable wreckage, of course), witnesses quake, and the
conqueror's awe rings out. The AI hand of a ruined god was already still.

**Game saves (`src/SaveFile.*`)** — `.sav` "GSAV" v1, little-endian, the
COMPLETE sim: terrain heights, day cycle, every prop field, every village
(stores, counters, buildings, farm cells, fields, fishing spots, private
founding rng), every villager field (including per-villager rng streams),
gods, and the AI brains (counters, timers, rng). Anything held by a hand is
settled in place first, so a save is always a valid world. A camera block
restores the view. Invariants, headless-enforced: save -> load -> save is
byte-stable; a loaded save checksums identical to the world that was saved;
and a loaded game **continues bit-for-bit like the original** (run the
source and the loaded copy N steps: identical checksums).

**Menus** — title (over a live ambient skirmish the sim keeps running):
CONTINUE (newest save), SKIRMISH with a map picker (random island or an
existing `maps/slot<N>.gmap`), SANDBOX, EDITOR, QUIT. Pause (world frozen):
RESUME, SAVE, LOAD, save-slot picker, MAIN MENU, QUIT (editor sessions show
only RESUME / MAIN MENU / QUIT). Keyboard (arrows + enter) and mouse (hover
+ click). Esc resumes from pause.

## Out of scope (later)

Difficulty presets in the menu (M8 decides the knobs), map thumbnails or
naming (editor v2), a third god, story scenarios (post-skirmish), sound.
