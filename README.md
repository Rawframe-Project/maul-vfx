# Maul VFX

Visual effects in C: particles, ribbons, bodies and fluids, simulated with the same bits on every platform and handed to any renderer as data.

[![CI](https://github.com/Rawframe-Project/maul-vfx/actions/workflows/ci.yml/badge.svg)](https://github.com/Rawframe-Project/maul-vfx/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/Rawframe-Project/maul-vfx)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Rawframe-Project/maul-vfx)](https://github.com/Rawframe-Project/maul-vfx/releases)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Rawframe-Project/maul-vfx/badge)](https://scorecard.dev/viewer/?uri=github.com/Rawframe-Project/maul-vfx)

A game or an engine that wants rich effects usually takes them from its engine's particle system, tied to that engine's renderer, editor and threads, with results that differ by platform and frame rate. Maul VFX is a standalone simulation that draws nothing and decides nothing for its host:

- **Particles** from emitters with rates, bursts, distance and events, born on points, lines, shapes and meshes; moved exactly under gravity and drag, by wind, attractors, vortices, curl-noise turbulence and vector fields; with values over life, kill rules and the host's own modules.
- **Collision** with planes, spheres, boxes, heightfields and distance grids, the host's scene through queries it answers, and the camera's depth image.
- **Interaction:** neighbor grids, flocking and steering, contacts and plexus links; ropes and cloth by XPBD that pin, tear, flutter and keep off themselves; rigid bodies that tumble, stack and sleep.
- **Fluids:** smoke and fire on grids, liquids by position-based fluids or APIC, surfaces meshed by marching cubes, and water surfaces of waves or shallow water that particles float on and pour into.
- **Output** in the renderer's own layout: records sorted stably, quads, ribbons, tubes, lights, sheets, meshes, gas and water fields, bounds and draw order.
- **Effects** built in code or read from a versioned binary file checked as hostile input, with quality levels and a particle budget.
- **The GPU part:** the same effects run in WGSL kernels on Vulkan, Direct3D 12, Metal or WebGPU through [Maul RHI](https://github.com/Rawframe-Project/maul-rhi), equal to the CPU path in every count and within stated tolerances in its floats.

The same effects, seeds, inputs and steps give the same bits on every platform and compiler, whatever the host's threads, so effects replay, roll back and agree across machines. Time is counted in ticks, so the result does not depend on how frames split time. All memory comes from the host's allocator when an object is made; stepping allocates nothing. Parallel work runs on the host's own task system.

## Quick start

It builds with CMake 3.25 or newer and GCC 14 or newer, Clang 19 or newer, or Apple Clang. On Linux and macOS:

```sh
git clone https://github.com/Rawframe-Project/maul-vfx.git
cd maul-vfx
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/sample_fountain
```

Ubuntu 24.04's default compiler is GCC 13, which the build refuses ("GCC 14 or newer is required"). Install GCC 14 and configure again in a fresh build directory, since `build/` keeps the compiler it first found; then go on from `cmake --build build`:

```sh
sudo apt install gcc-14
rm -rf build && CC=gcc-14 cmake -B build -DCMAKE_BUILD_TYPE=Release
```

On Windows the library builds with `clang-cl`, and on the web with Emscripten; [Getting started](docs/user/getting-started.md) lists what each needs.

Install the library with `cmake --install build --prefix <prefix>` and use it from CMake:

```cmake
find_package(maul-vfx REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-vfx::maul-vfx)
```

or through pkg-config as `maul-vfx`. [Getting started](docs/user/getting-started.md) makes a first effect; `-DMAUL_VFX_RHI=ON` adds [the GPU part](docs/user/gpu.md).

## Platforms

Linux (x86-64 and arm64), macOS, Windows and the web (Emscripten). The library is C23 and builds with GCC 14 or Clang 19 and newer, and with `clang-cl` on Windows; its public headers compile as C17, C23 and C++17. It depends on nothing beyond the C library. CI builds and tests every platform, under AddressSanitizer, UBSan and ThreadSanitizer, and the GPU part on lavapipe under the Vulkan validation layer.

## Documentation

- [The documentation site](https://rawframe-project.github.io/maul-vfx/), with the API reference generated from the headers.
- [User documentation](docs/README.md): one page per part of the library.
- [How it works](docs/internals/overview.md) and [how to develop it](docs/operations/development.md).
- [Changelog](CHANGELOG.md).

Maul VFX belongs to the Maul family of standalone C libraries, which share one set of conventions.

## Contributing

Issues are welcome. Pull requests are accepted for issues a maintainer has approved; see [CONTRIBUTING.md](https://github.com/Rawframe-Project/.github/blob/main/CONTRIBUTING.md). The project is developed with AI coding agents, directed and reviewed by its maintainers; outside contributions follow the [AI policy](https://github.com/Rawframe-Project/.github/blob/main/AI_POLICY.md).

## License

MIT; see [LICENSE](LICENSE).
