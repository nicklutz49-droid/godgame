# Slice plan: light & shadow (M9)

The graphics pass. Everything stays GL 3.3 core, llvmpipe-playable, and
procedural - no textures from disk, no new hard dependencies.

## Locked decisions

- **The world casts real shadows; crowds keep their blobs.** A 1024 depth
  map rendered from the sun (terrain, buildings, temples, trees, rocks,
  stumps); villagers and loose props keep the soft blob discs.
- **Night point lights**: up to six - campfires flicker warm, temple
  beacons glow in their god's color.
- **Atmosphere**: water sun-glitter, baked terrain ambient occlusion,
  deeper villager LOD. (Shore foam deferred.)
- **llvmpipe stays playable**: modest map size, capped lights, `F7`
  toggles shadows entirely.

## How it works

- `gl.h` grew the texture/framebuffer/polygon-offset entry points; the
  depth pass is a two-line shader (position -> light clip space) into a
  depth-only FBO with hardware compare (LINEAR = free 2x2 PCF) and a white
  border (off-map = lit).
- The sun's ortho box (190 m half-extent) follows the camera focus and is
  texel-snapped so shadows don't shimmer when panning. Strength fades in
  with sun height (no grazing-angle acne), and the pass is skipped entirely
  at night. Acne control: polygon offset in the caster pass + a small
  constant bias; peter-panning stays acceptable at this art scale.
- The lit shader samples 4 PCF taps (x the hardware 2x2), shadows the sun's
  diffuse term fully and the ambient term gently (x0.78) so noon shadows
  still read.
- Point lights: `uLightPos/uLightCol[6]` with quadratic falloff, gathered
  per frame (nearest six to the camera focus), only when night > 0.05.
- Terrain AO is baked into vertex colors at mesh-build time from
  neighbourhood concavity. Water glitter perturbs the wave normal with fine
  ripples and sharpens the sun glint into sparkles, gated by sun height.
- Villager LOD: torso-only beyond 180 m; thought bubbles only within 140 m.

## Measured (llvmpipe, xvfb, 4 shared cores, 1280x720 village view)

- shadows on: ~45 ms/frame; shadows off (F7): ~23 ms; night: ~26 ms.
  A real GPU renders this scene without noticing. The toggle is the
  contract with weak machines.

## Out of scope

Shore foam ribbon, bloom, cascaded shadow maps, textured terrain (that
arrives with the asset import, plan-assets.md §5).
