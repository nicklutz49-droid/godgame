# godgame — importing original Black & White assets

The project was built from day one so that original assets from the owner's
personal copy of Black & White (2001) can replace the procedural
placeholders WITHOUT touching the sim: `Terrain` hides the heightfield
source, every model is a `MeshData` handed to `Mesh::upload`, and all
gameplay reads go through those interfaces. This document is the
implementation plan for actually doing it.

## 0. Ground rules (read first)

- **Personal use only. No distribution.** Assets are read AT RUNTIME from
  the user's own installation (`--bw <install-dir>`); nothing extracted or
  converted is ever committed to this repository, packaged, or uploaded.
  The repo ships procedural placeholders forever; original assets are a
  local overlay. Loader code (clean-room, format knowledge from the
  openblack community) is fine to commit; asset BYTES are not.
- **Graceful absence**: every loader falls back to the placeholder when the
  file is missing/unparseable. `--headless` never requires assets; CI-ish
  checks stay asset-free. A `bwassets::available()` gate keeps call sites
  honest.
- **The sim never changes.** Asset import is 100 % render/data-side. World
  gen from an LND heightfield must produce the same Terrain interface the
  procedural generator fills; determinism rules apply unchanged (the
  heightfield is DATA, not code).

## 1. Where the files live (B&W 1 install)

```
<install>/
  Data/
    Landscape/Land1.lnd .. Land5.lnd, MultiPlayer maps  (terrain + terrain textures)
    AllMeshes.g3d          (every model: villagers, buildings, trees, misc - L3D meshes in a pack)
    AllAnims.anm           (skeletal animations pack)
    Audio/                 (music + sfx packs, .sad)
    Symbols/, Misc/, CreatureMesh/ ... (gesture art, creature - not needed, no creature here)
  Scripts/                 (.chl compiled Lionhead scripts - reference only, we have our own scenarios)
```

Exact block layouts: see §7 Format reference. The authoritative community
source is the **openblack** project (github.com/openblack) — clean-room
parsers for LND/L3D/ANM/pack formats with permissive licensing; treat their
readers as the spec, not code to copy verbatim (different architecture).

## 2. Architecture: a `bwassets` module

New sim-side-safe module (no GL — returns plain data):

```
src/bw/BWPack.*      pack/container reader (blocks by name, bounds-checked)
src/bw/BWLand.*      .lnd -> heightfield grid + per-cell material/country + terrain textures (raw pixels)
src/bw/BWMesh.*      .g3d/.l3d -> MeshData-compatible geometry + texture refs (+ optional skin data)
src/bw/BWAnim.*      .anm -> bone tracks (phase 2)
src/bw/BWAudio.*     .sad -> PCM blobs (phase 2)
src/bw/BWAssets.*    the facade: BWAssets::init(installDir) scans + indexes;
                     everything else asks it. Missing dir = inert.
```

Rules carried over from MapFile/SaveFile: little-endian `serial::Reader`
style bounds-checked reads, validate-before-use, return `false` and leave
outputs untouched on malformed data, headless-testable with tiny synthetic
fixtures (hand-built 2-cell LND / 1-triangle L3D buffers committed as C
arrays in the TEST, never real game bytes).

## 3. Phase A — terrain (the biggest visual win)

1. `BWLand::load(path)` → `{ gridN, heights[], cellMaterial[], textures }`.
2. B&W landscapes are island heightfields around ~160x160 cells of ~10 ft
   spacing — RESAMPLE (bilinear) onto our `Terrain::GRID` (256) x `SIZE`
   (512 m) so every downstream constant (work radius, influence rings,
   physics) keeps meaning. Water level aligns to our y=0 by offsetting the
   LND altitude scale; clamp the seabed to ours (-14).
3. Entry points:
   - CLI: `godgame --bw <dir> --land 1` → `terrain.setHeights(resampled,
     seed)` then the standard founding scan (`findVillageSites`) populates
     it — B&W ground, our villages. (Later: parse the LND's own village/
     feature placements if desired — v2.)
   - Editor: `L` in the editor loads a land as sculpting clay; SAVE writes a
     normal `.gmap` (heightfield is copied, so .gmap stays self-contained
     and shareable-with-self — no asset bytes inside, just terrain shape...
     note: heights ARE derived from the asset; keep such .gmaps local too).
4. Terrain coloring: phase A keeps our painterly height/slope vertex colors
   (zero renderer changes). Phase A+ blends the LND per-cell material as a
   vertex-color tint. TRUE textured terrain needs §5.

Tests: synthetic LND fixture round-trip; resample conservation (min/max/
mean within tolerance); `--bw` absent → procedural fallback intact.

## 4. Phase B — meshes (buildings, trees, villagers)

1. `BWPack` opens `AllMeshes.g3d`, indexes L3D blocks by mesh id.
2. `BWMesh::load(id)` → positions/normals/uvs + submesh texture ids. Two
   consumption modes:
   - **Vertex-color mode (first)**: sample each vertex's texel → bake into
     our pos/normal/color layout. Zero shader changes, instantly works with
     the whole pipeline (tints, ghosts, fog). Low-res but very much the
     placeholder aesthetic upgraded.
   - **Textured mode**: after §5, upload real UVs + textures.
3. Mapping table `src/bw/BWMeshMap.h`: our placeholder builder → B&W mesh
   name(s), e.g. `models::houseStage(3)` → `BUILDING_CELTIC_1`, totem →
   `BUILDING_TOTEM`, storage → `FEATURE_...`; trees → `TREE_*` variants;
   villager parts are trickier (B&W villagers are single skinned meshes,
   not our 6-part puppet) — see §6.
4. `App::initScene` asks `BWAssets` first for every mesh; placeholder
   builder on miss. `--no-bw` forces placeholders for A/B comparisons.
5. Scale/axis: B&W uses a different unit scale and axis convention —
   normalize inside BWMesh (one constant, measured against a known object:
   a house should span ~6-8 m in our world) so call sites never know.

Tests: synthetic L3D fixture; a mapping-table completeness check (every
placeholder id resolves to SOME entry, even if "none"); scale sanity
(loaded house bbox within expected meters) — all skipped without `--bw`.

## 5. Renderer growth: one texture path (prerequisite for true fidelity)

Smallest honest step, kept llvmpipe-safe (GL 3.3 core, no extensions):

- `Mesh` learns an OPTIONAL vertex layout v2: pos3/normal3/color3/uv2, and
  an optional owned `GLuint texture` (one per mesh; submeshes with distinct
  textures become separate Meshes — matches our one-draw-per-thing model).
- The lit shader gains `uniform sampler2D uTex; uniform float uTexMix;`
  with `albedo = mix(vColor * uTint, texture(uTex, vUv).rgb * uTint,
  uTexMix)`. `uTexMix=0` for every existing draw — pixel-identical output
  (guard: screenshot diff before/after on the procedural island).
- Texture upload helper honoring the pack's pixel formats (see §7; expect
  palettized and/or 16-bit 565/1555 — convert to RGBA8 on load).
- Fonts/HUD/water/sky untouched.

## 6. Phase C — villagers & animation (hardest, optional-est)

Our villagers are a 6-part procedural puppet driven by `computeVillagerPose`
(pure math, also the headless-tested bit). B&W villagers are skinned meshes
with ANM skeletal clips. Two viable levels:

- **C1 static-part swap**: keep the puppet, replace each part's placeholder
  box with a CHOPPED piece of the B&W villager mesh (head/torso/limbs split
  by bone weights at load). Keeps every existing animation & test. Cheap,
  looks 80 % right at our camera heights.
- **C2 true skinning**: new render path — bone palette (mat4 array uniform,
  ≤ 30 bones fine in GL 3.3), ANM playback state per villager (render-side
  map from VState/job/walkPhase → clip + time), `computeVillagerPose`
  remains the sim-facing truth and drives root transform + clip choice.
  Do NOT let clip timing feed back into the sim (firewall).

Do C1; measure appetite before C2. Trees/buildings from Phase B already
carry most of the fidelity.

## 7. Format reference (verified against openblack)

> Maintainer note: layouts below were cross-checked against the openblack
> parsers at research time; when in doubt, re-read their source — repo:
> github.com/openblack/openblack (file parsers under components/ or src/,
> e.g. LNDFile, L3DFile, PackFile, ANMFile). RESEARCH-FILLED — see the
> appended notes at the bottom of this file for the verified details and
> open questions a loader author must resolve.

- **LND**: header + named blocks; per-cell altitude bytes with a global
  altitude scale; 16x16-cell leaf blocks; country/material tables; embedded
  terrain texture pages.
- **G3D pack / L3D**: "LiOnHeAd" pack container of named blocks; MESHES
  block = array of L3D; L3D header → submesh list → primitive groups →
  vertex arrays (pos/uv/normal) + skin/bone tables; textures referenced by
  id into the pack's texture blocks.
- **ANM**: header + per-bone keyframe tracks (rot/pos), frame count + fps.
- **SAD audio**: paired metadata/data blocks containing named samples
  (PCM/ADPCM) — decode to 16-bit PCM for our mixer.

## 8. Delivery order & acceptance

1. §2 BWPack + §3 BWLand (behind `--bw`); screenshot: our villages on Land1.
2. §4 vertex-color meshes for trees + the building roster; A/B screenshot.
3. §5 texture path (guarded by a procedural-island pixel-diff), then
   textured terrain + meshes.
4. §6 C1 villager parts; §8 audio swap-in behind `Sound::play` ids
   (plan-next.md §8 must land first).
5. Each step: headless synthetic-fixture tests, fallback verified by
   running WITHOUT `--bw`, README note, no asset bytes in git
   (`.gitignore`: `/bw-cache/` if any conversion caching is added).

Acceptance for "the assets are in": launch `godgame --bw ~/BlackWhite
--land 1`, see the real island, real buildings and trees, villagers wearing
real meshes (C1), placeholders nowhere in sight — then delete the flag and
get the procedural game back, bit-identical to today.
