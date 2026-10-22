# Samples

Small programs in `samples/`, built with the library when it is the
top-level project (`MAUL_VFX_BUILD_SAMPLES`). The library draws nothing,
so each writes what a renderer would draw and checks it: each returns 0
when what it shows holds, and runs as a test.

| Program | What it shows |
|---|---|
| `fountain.c` | An emitter spraying water up in a cone under gravity and a side wind, fading and growing over life, bouncing on the ground. Each second it writes a frame: the particles sorted back to front from a camera, and the same particles as camera-facing quads. |
| `trails.c` | A tapering trail pinned to an instance circling the origin, and a beam the host aims from it at a point each frame through an instance parameter. Each second it writes the trail's points and both ribbons expanded into strips, and checks the trail is as long as the arc its source covered in a lifetime and the beam spans from source to target. |
| `fireworks.c` | Rockets launched in bursts whose deaths are events, each bursting into forty sparks that keep some of the rocket's velocity. The host reads the deaths and the sparks' births after every step and checks every rocket made exactly its sparks. |
| `flock.c` | Sixty birds that gather by separation, alignment and cohesion and follow a closed path round a square, with two pillars on it as colliders. It reports the flock's order, the birds' distances from the path and their contacts with the pillars. |
| `cloth.c` | A sheet of 24 by 24 points dropped onto a ball, solved in XPBD substeps and colliding with the ball and the ground, expanded into a mesh with normals each second. Given a path it writes the last mesh as an OBJ file. |
| `smoke.c` | A 3D gas over a fire: fuel burns into heat and smoke, the heat lifts it, vortex confinement keeps its curls, and embers ride its velocity up. Each second the host reads the density as unorm8, as for a volume texture, and finds how high the smoke has risen; given a path it writes the middle slice as a PGM image. |
| `liquid.c` | Five hundred particles poured into a tank, solved as position-based fluids, with a surface gas wrapping them that the host meshes by marching cubes each second. Given a path it writes the last mesh as an OBJ file. |
| `pond.c` | A water surface of waves with thirty leaves floating on it, pushing it where they land, and a stone the host throws in. Each frame the host samples the surface under a boat to bob it, and checks the leaves float on the surface and the stone's waves reach the boat. |
| `effect_file.c` | An effect whose force reads a vector field, a grid of the host's named through an asset table, written as a file, read back to the same bytes, and made into an effect that plays to the byte as the one made in code; a damaged copy is refused at the hash that no longer matches. Given a path it writes the file there. |
| `gpu.c` | The fountain played twice in one context, once on the CPU and once planned and simulated on a device through Maul RHI, their live counts and bounds compared each second. Built with `MAUL_VFX_RHI`; skipped without an adapter. |
| `minimal/` | A separate project that uses only the installed package, in C17: it checks the linked version, makes a fountain, steps it for a second and writes its particles. CI builds it against a fresh install. |

Build the minimal sample against an install:

```sh
cmake --install build --prefix "$PWD/package"
cmake -S samples/minimal -B build-minimal -DCMAKE_PREFIX_PATH="$PWD/package"
cmake --build build-minimal
./build-minimal/minimal
```
