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

## 3. Phase A — terrain (the biggest visual win) — SHIPPED (M10)

> Landed as `src/bw/BWLand` + `World::generateOnCurrentTerrain` +
> `App::rebuildWorldOnLand`. Entry points: `--land N`, the title-menu map
> picker (choices 5..9 = LAND 1..5), and `L` in the editor. The waterline
> self-calibrates from the mean coastline-cell altitude instead of a hand
> offset; `tune::kBwLandHeightScale` (0.22) awaits real-install
> calibration. Split-diagonal sampling and the water clamp are covered by
> headless test [18] on synthetic fixtures.

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

## 4. Phase B — meshes (buildings, trees, villagers) — SHIPPED (M10, vertex-color mode)

> Landed as `src/bw/BWPack` + `BWMesh` (DXT1/DXT3 decode, texel-baked
> vertex colors, CW→CCW rewind, no V flip needed for a CPU bake of
> top-down DDS rows) + the `BWMeshMap.h` slot table (Celtic set) +
> `App::uploadWorldMeshes` (F8 toggles; swaps at upload only). Villagers
> stayed procedural by decision (§6 C1 is its own slice). Textured mode
> still needs §5. `tune::kBwMeshScale` (0.1 = the landscape's own
> unit-to-meter ratio) awaits real-install calibration via `--bw-report`
> bounding boxes.

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

## 7. Format reference (verified against the openblack parsers)

> Verified 2026-07 by reading openblack's actual source
> (github.com/openblack/openblack: components/lnd, components/pack,
> components/l3d, src/3D) plus its wiki and the bw1-decomp original-symbol
> structs. When anything below conflicts with reality, the openblack
> parsers are the tiebreaker. **ANM (animations), SAD (audio), and the
> definitive install-layout notes were NOT yet researched** (the research
> agents hit a rate limit) — a follow-up pass must fill them from openblack's
> ANMFile and audio loaders before Phases C/audio start.

### 7.1 LND landscape (`Data/Landscape/*.lnd`)

Little-endian raw C-struct dump, no magic, no compression. Section order:
**header (1052 B) → low-res texture records → (blockCount−1)×2520 B blocks →
countries (×3076 B) → materials (×131074 B) → noise map (65536 B) → bump
map (65536 B) → optional tail bytes** (preserve/ignore). Validate
`blockSize==2520, materialSize==0x20002, countrySize==3076`.

- **Header**: `u32 blockCount` @0 (stored records = blockCount−1 — block 0
  = ocean, not stored); `u8 lookUpTable[1024]` @4 — a 32×32 grid,
  `lookUpTable[blockX*32+blockZ]`, 0 = open sea, else 1-based index into
  the stored blocks; `u32 materialCount` @1028, `countryCount` @1032, then
  the three size fields and `lowResolutionCount` @1048.
- **Low-res texture records** (skip them): 20 B header `{texture, material,
  unknown, index, size}` + `(size−4)` bytes of DXT3 texels — the `size`
  field INCLUDES itself. openblack never renders these.
- **Block (2520 B)**: `LNDCell cells[17*17]` @0 (8 B each, indexed
  `cells[cellX*17 + cellZ]`, 17×17 vertices of a 16×16-quad tile — resolve
  edges via neighbour blocks, the 17th row/col is unreliable); `u32 index`
  @2312 (1-based, matches lookUpTable); `f32 mapX,mapZ` @2316 (recompute as
  blockX*160 instead of trusting); `u32 blockX,blockZ` @2324/2328; the rest
  is runtime state, all zero on disk.
- **Cell (8 B)**, confirmed by original symbols: `u8 r,g,b` (baked vertex
  color, often 0), `u8 luminosity`, `u8 altitude` (**world Y = altitude ×
  0.67**, unsigned — sea is faked by flags), `u8 saveColor`, `u8 properties`
  (bits 0-3 = country index, 0x10 hasWater, 0x20 coastLine, 0x40 fullWater,
  0x80 split), `u8 flags` (sound class). Water: hasWater/fullWater ⇒ land
  alpha 0; coastLine ⇒ 0.5. **Split bit** picks the quad diagonal
  (0: TL-BR, 1: BL-TR) — needed for exact ground sampling.
- **Country (3076 B)**: `u32 type` + 256 × `{u32 mat0, u32 mat1, u32 coeff}`
  — one entry per altitude 0-255; texture = mix(mat0, mat1, coeff/255),
  perturbed by the noise map: `entry = (altitude + noise[cell]) % 256`.
- **Material (131074 B)**: `u16 type` + 256×256 `u16` texels in **A1R5G5B5**.
- **Noise then bump** (openblack's read order): two raw 256×256 R8 images.
- **Conventions**: 1 cell = 10 world units, 1 block = 160, max island
  5120×5120. Global cell (x,z ∈ [0,511]): `block = lut[(x>>4)*32+(z>>4)]`,
  `cell = blocks[block-1].cells[(x&15)*17+(z&15)]`. openblack's height
  lookup is nearest-cell; interpolate across the split triangles yourself.
- **Our resample** (§3): B&W's 5120-unit, 0..170.85 m island → our 512-unit
  grid: scale XZ by 0.1, choose a height scale that keeps beaches at our
  y≈0-2 (start: `ourY = altitude*0.67*0.35 − seaOffset`, tune by eye), water
  cells clamp below 0.

### 7.2 G3D pack + L3D meshes (`Data/AllMeshes.g3d`)

- **Pack container**: `char[8] "LiOnHeAd"`, then blocks to EOF:
  `{char[32] name (NUL-padded), u32 bodySize}` + body. Mesh packs contain
  many texture blocks (block name = lowercase hex of the texture id),
  `INFO`, `MESHES`, and an ignorable low-res block.
- **INFO**: `u32 numTextures` + `{u32 blockId, u32 unknown}` each.
- **Texture block**: `{u32 size, u32 id, u32 type, u32 ddsSize}` + a DDS
  file minus its 4-byte magic (124-byte DDS_HEADER + texels). DXT1/DXT3
  (type 1/2). `id` is the key L3D materials reference via `skinID`.
  DDS_HEADER offsets that matter: height @8, width @12, DDS_PIXELFORMAT
  @72, its fourCC @80, texels @124 (verified the hard way in test [18]).
- **MESHES**: `"MKJC"`, `u32 meshCount`, meshCount × `u32 offset` (relative
  to the MESHES body; size each mesh by the gap to the next offset, last one
  runs to the block end), L3D blobs packed tight. **Index = fixed MeshId** —
  the vanilla 626-entry enum is openblack's `src/3D/AllMeshes.h`
  (0=Dummy … 625=U_WashingLineTibetan); `BWMeshMap.h` carries just the
  bound subset (names as comments), not the whole enum.
- **L3D blob**: 76 B header: `"L3D0"`, flags, size, submeshCount,
  submeshOffsetsOffset, zeroed bbox, 0xFFFFFFFF, skinCount,
  skinOffsetsOffset, extraDataCount/Offset, footprintDataOffset. Offsets
  are ABSOLUTE within the blob; 0xFFFFFFFF = absent; v1.00 skin offsets
  equal to fileSize ⇒ skip. Flag bits worth honoring: HasBones 0x100,
  NoDraw 0x2000, ContainsLandscapeFeature 0x8000.
- **Submesh** header 20 B `{flags, numPrimitives, primitivesOffset,
  numBones, bonesOffset}`; primitivesOffset → a table of u32 offsets → each
  a 48 B **primitive**: material `{u32 type, u8 alphaCutout, u8 cullMode,
  u16 pad, u32 skinID, u32 colorBGRA}` + vertex/triangle/group/blend counts
  & offsets. Submesh flags: draw only `status==0` (bits 4-9) and LOD bit 0;
  skip `isPhysics` (bit 13). Material `skinID 0xFFFFFFFF` = untextured.
- **Vertex 32 B**: float3 pos, float2 uv, float3 normal — maps 1:1 onto our
  layout (color from texture bake in mode 1). **Triangles**: u16×3, indices
  local to the primitive (re-base when merging). **Bones 60 B**: parent /
  firstChild / rightSibling + 3×3 float rotation + float3 position,
  parent-relative (row/col-major of the 9 floats: verify on a posed model).
  **Vertex groups** `{u16 count, u16 boneIndex}` = run-length rigid skinning
  (enough for §6 C1 part-chopping).
- **Render gotchas**: D3D top-left UV origin ⇒ flip V for GL; openblack
  culls CW-front-equivalent — verify winding on one known building and set
  our importer to emit CCW; expand embedded 4444/1555 16-bit textures to
  RGBA8 at load.

### 7.3 Still to research (blocked on agent budget, not on design)

- **ANM** animation tracks (openblack `ANMFile`) — needed for §6 C2 only.
- **SAD** audio packs (openblack audio loaders) — needed for §8 sound swap.
- Install-layout edge cases across retail/GOG patches, and community norms
  documentation. The §1 layout above is believed-correct but unverified.

## 8. Delivery order & acceptance

1. ~~§2 BWPack + §3 BWLand (behind `--bw`)~~ SHIPPED (M10).
2. ~~§4 vertex-color meshes for trees + the building roster~~ SHIPPED (M10).
   Both verified here only on synthetic fixtures + a procedural
   pixel-identity diff; the FIRST RUN against a real install still needs
   doing — `--bw <dir> --bw-report`, then eyeball a `--land 1` boot and
   F8 A/B, then calibrate kBwMeshScale/kBwLandHeightScale and rebind any
   ugly slots in BWMeshMap.h.
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
