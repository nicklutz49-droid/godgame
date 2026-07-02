# godgame

A Black & White inspired god game, written from scratch in C++ / OpenGL / SDL2.
Personal project — the code is an original recreation, and original game assets
(from a personally owned copy) may be wired in later. Everything currently on
screen is procedural placeholder art. There is no creature, by design.

![The village](docs/village.png)
![The village at night](docs/night.png)
![The island](docs/island.png)

## Current slice: a living village

The island now has one village and its people (see
[docs/plan-villagers.md](docs/plan-villagers.md) for the full design):

- **Villagers** — needs (hunger, energy), jobs (forester, farmer, fisherman,
  builder), and idle lives (wandering, chatting, sitting by the fire).
  Foresters fell trees with real physics and haul the logs home; farmers tend
  and harvest a crop field; fishermen cast from shore spots; builders haul
  wood and raise new houses through visible construction stages. Surplus food
  means children at dawn.
- **The hand and the people** — pick villagers up (they flail and panic),
  throw them (they tumble, get stunned, stagger up, and flee), or set them
  down gently *on* something to assign a job: trees → forester, the field →
  farmer, water → fisherman, a construction site → builder. Drop logs, food,
  or entire uprooted trees onto the storage pad to stock the village.
  Villagers notice the hand looming and cower if they've learned to fear it.
- **Day & night** — the sun arcs across the sky, dusk turns the fog salmon,
  nights are dark with stars, window glow, and the campfire; villagers head
  home at dusk (the homeless curl up by the fire) and emerge at dawn.
- **Procedural island** — seeded heightfield (fBm + ridged noise, irregular
  coastline) with painterly coloring, animated ocean, and fog. The village
  founds itself on the best coastal terrace and gently terraforms it flat —
  same seed, same island, same village, on every platform.
- **The camera** — grab-the-land panning, zoom toward the cursor, orbit/tilt,
  pitch eased by altitude, just like the original.

There is no belief/worship yet (next slice) and villagers cannot die —
hard landings stun instead. The mortality seams are in place for later.

## Controls

| Input | Action |
| --- | --- |
| Left-drag on ground | Pan (grab the land) |
| Left-drag on thing | Pick up rock / tree / log / food / **villager** |
| ...release while still | Set down — on trees/field/water/site = assign job |
| ...release mid-motion | Throw |
| Right- or middle-drag | Rotate & tilt camera |
| Mouse wheel | Zoom toward cursor |
| `W A S D` / arrows | Move camera (Shift = faster) |
| `Q` / `E` | Rotate camera |
| `T` | Advance time of day |
| `R` | Generate a new island |
| `F2` | Wireframe |
| `Esc` | Quit |

Debug/tuning: `F3` tint villagers by AI state, `F4` sim speed ×1/4/16,
`K` spawn a villager at the hand, `L` +10 wood & food. Gameplay constants
live in `src/Tuning.h`.

## Building

Requires CMake 3.16+ and a C++20 compiler. SDL2 and glm are found on the
system when available and otherwise fetched and built automatically.

### Linux

```sh
sudo apt install libsdl2-dev libglm-dev   # optional but faster
cmake -B build && cmake --build build -j
./build/godgame
```

### Windows

Open the folder in Visual Studio (CMake project) or run:

```sh
cmake -B build
cmake --build build --config Release
build\Release\godgame.exe
```

The first configure downloads and builds SDL2; later builds are fast.

## Command line

```
godgame                       play
godgame --seed 1234           play a specific island
godgame --headless [steps]    no window: world-gen, physics, village economy,
                              hand-interaction and determinism self-tests
godgame --screenshot out.bmp [frames] [far|close|village|night]
```

## Roadmap

- Worship, belief, and the influence ring
- Miracles: gesture casting, water/fire/food
- Villager mortality (throws, drowning, starvation — seams already in place)
- Loaders for original Black & White data files (terrain, meshes, textures)
- Sound

## Layout

```
src/
  main.cpp       app loop, input, rendering, placeholder models, CLI modes
  gl.h/.cpp      minimal OpenGL 3.3 core loader (via SDL_GL_GetProcAddress)
  Shader.*       shader compile/link + uniform helpers
  Mesh.*         interleaved VAO/VBO wrapper + primitive builders
  Models.*       village-slice mesh builders (villagers, buildings, bubbles)
  Noise.h        deterministic value noise / fBm / ridge + XorShift RNG
  Terrain.*      island heightfield: generate, flatten, sample, raycast, mesh
  Physics.*      ballistic sphere step shared by props and thrown villagers
  Water.*        animated transparent ocean
  Sky.*          gradient sky + sun, night palette + stars
  DayCycle.h     time of day: sim schedule + lighting curves
  Camera.*       B&W-style camera
  World.*        props (trees/rocks/logs/food), physics, scattering
  Village.*      settlement: founding, layout, buildings, farm, stores
  Villager.h     villager data (jobs, states, needs)
  Villagers.*    villager AI: priority ladder, job loops, steering, poses
  Hand.*         the divine hand: hover, grab, carry, throw, assign
  Tuning.h       every gameplay constant
```
