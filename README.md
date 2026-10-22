# Maul VFX

Visual effects for C and C++ programs: particles, ribbons, trails and
beams, flocks, ropes and cloth, rigid bodies, smoke and fire, liquids
and water, simulated on the CPU with the same bits on every platform,
and handed to any renderer as data. Written in C23 with public headers
any C17 or C++17 program can include, with no dependency and an MIT
license. An optional part runs the same effects on the GPU through Maul
RHI.

It covers:

- **Emission:** rates and curves of rates, bursts, spawning by distance
  moved, from another emitter's events and by the host; points, lines,
  circles, spheres, boxes, cones and meshes, on their surfaces or in
  their volumes.
- **Motion:** gravity and drag integrated exactly, wind, attractors,
  vortices, curl-noise turbulence (analytic, or baked into a periodic
  grid), vector fields, a gas's velocity, velocity and values over each
  particle's life, kill rules, and modules a host writes itself.
- **Collision:** planes, spheres, boxes, heightfields and signed
  distance grids, the host's scene through queries it answers between
  steps, and the camera's depth image.
- **Interaction:** neighbor grids, flocking and steering (seeking,
  fleeing, following paths), contacts and forces between particles, and
  plexus links.
- **Bodies:** ropes and cloth by XPBD (pinned, held to the instance,
  the host's frames or other particles, torn, with aerodynamics and
  self-collision) and rigid particles (spheres, capsules, boxes and
  convex hulls) that tumble, roll, stack and sleep.
- **Fluids:** smoke and fire on grids (buoyancy, combustion, vortex
  confinement, obstacles), liquids (position-based fluids, or particles
  on a grid by APIC), surfaces of particles meshed by marching cubes,
  and water surfaces (waves, and shallow water over terrain) that
  particles float on, push and pour into.
- **Ribbons:** trails from a moving source, trails of particles,
  ribbons through groups, beams with bends and noise; tessellated,
  textured and faced as the renderer needs, or expanded into strips and
  tubes.
- **Output:** instance records in the layout the renderer asks for,
  ribbon points, lights, sprites, sheets and surfaces expanded into
  vertices, gases and water read into texture formats and sampled at
  points, stable sorting, bounds and draw order. The library draws
  nothing.
- **Effects** built in code or read from a versioned binary file that
  is checked as hostile input; quality levels and a particle budget.
- **The GPU part** (`maul-vfx-rhi`): a planned instance's particles,
  gases and water surfaces simulated in WGSL kernels compiled for every
  Maul RHI backend, its results the CPU path's within a stated
  tolerance, with records, ribbons, sprites, sheets, lights, reads,
  samples and meshes written on the device.

Results are identical on every platform, whatever the host's threads.
Everything lives in a context the host creates with its allocator;
stepping allocates nothing.

## Status

Not released yet; 0.1.0, the whole scope above, is being prepared.
Everything listed is built and tested on every CI platform (Linux
x86-64 and arm64 with GCC and Clang, under the address, undefined
behaviour and thread sanitizers, as a shared library, macOS, Windows
with clang-cl, and the web), the GPU part on lavapipe under Vulkan's
validation layer. Fuzz targets cover the effect file's readers and the
step of whatever effects they accept.

## Documentation

- [The guide](docs/guide.md) walks through effects, instances, output,
  ribbons, events, interaction, fluids, collision, the GPU part and
  building; every C block in it is run by the tests.
- [The API reference](docs/api.md) lists all 154 public functions,
  generated from the headers.
- [The samples](docs/samples.md) are small complete programs, each run
  as a test.
- [The changelog](CHANGELOG.md) records every change.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

`-DMAUL_VFX_RHI=ON` adds the GPU part, which finds Maul RHI installed or
fetches its release; its kernels come compiled for every backend, so a
build needs no shader tools.

Effect files are binary (`maul-vfx/file.h`); `tools/effect_json.py`
converts one to a JSON form and back (`to-json`, `to-binary`) for diffs,
reviews and test fixtures, which the library never parses:

```sh
python3 tools/effect_json.py to-json effect.mvfx effect.json
python3 tools/effect_json.py to-binary effect.json effect.mvfx
```

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mvfx.md`.

## License

MIT; see `LICENSE`.
