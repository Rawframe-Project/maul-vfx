# Maul VFX

Visual effects for C and C++ programs: particles, ribbons, trails and
beams, simulated on the CPU with the same bits on every platform, and
handed to any renderer as data. Written in C23 with public headers any
C17 or C++17 program can include, with no dependency and an MIT
license. An optional part runs the same effects on the GPU through Maul
RHI.

It covers:

- **Emission:** rates and curves of rates, bursts, spawning by distance
  moved, from another emitter's events and by the host; points, lines,
  circles, spheres, boxes, cones and meshes, on their surfaces or in
  their volumes.
- **Motion:** gravity and drag integrated exactly, wind, attractors,
  vortices, curl-noise turbulence, vector fields, velocity and values
  over each particle's life, collision with shapes, grids and the
  host's scene, kill rules, and modules a host writes itself.
- **Ribbons:** trails from a moving source, trails of particles,
  ribbons through groups, beams with bends and noise; tessellated,
  textured and faced as the renderer needs.
- **Output:** instance records in the layout the renderer asks for,
  ribbon points, lights, an optional vertex expansion, stable sorting,
  bounds and draw order. The library draws nothing.
- **Effects** built in code or read from a versioned binary file that
  is checked as hostile input.

Results are identical on every platform, whatever the host's threads.
Everything lives in a context the host creates with its allocator;
stepping allocates nothing.

## Status

Not released. The design is decided; the context, the library's own
math and random numbers, curves, effects, instances, the step, emitter
lives, spawning (shapes, meshes, value sources, initial values, moving
instances, host spawns and prewarm), the integrator (exact under
gravity and drag) and forces (constant, wind, attractors, vortices,
turbulence, vector fields from grids, with falloffs) and velocity
modules (motion scale, added, orbit and radial velocity, a speed limit)
values over life (colour, alpha, size, flipbook, rotation speed,
custom attributes), kill rules, host modules and collision with planes,
spheres, boxes, heightfields, distance grids, the host's scene (by
queries it answers between steps) and a depth image, events (births,
deaths and collisions the host reads after a step) and emitters
spawning from other emitters' events are in place, and output comes
next.
It has 45 public functions.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mvfx.md`.

## License

MIT; see `LICENSE`.
