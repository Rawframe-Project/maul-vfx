# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.0.1](https://github.com/Rawframe-Project/maul-vfx/compare/v0.0.1...v0.0.1) (2026-10-23)


### Fixed

* **api:** stamp the root def with the family's ABI version ([#7](https://github.com/Rawframe-Project/maul-vfx/issues/7)) ([4d8d691](https://github.com/Rawframe-Project/maul-vfx/commit/4d8d69121a3cdb7d22dcdacb3fdd7d1f7dcca9e2))
* **ci:** report rhi test failures and wait out first-frame compiles ([#3](https://github.com/Rawframe-Project/maul-vfx/issues/3)) ([892ce0b](https://github.com/Rawframe-Project/maul-vfx/commit/892ce0bd6cc97091062c666cfd0b7fc27ea2b1d0))


### CI

* move to the organization's shared workflows and CI image ([#8](https://github.com/Rawframe-Project/maul-vfx/issues/8)) ([c9be0fa](https://github.com/Rawframe-Project/maul-vfx/commit/c9be0fa42e9fa2648cb89894afae2b19549a48db)), closes [#5](https://github.com/Rawframe-Project/maul-vfx/issues/5)

## [0.0.1](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.0.1)

The first release: visual effects simulated on the CPU with the same
bits on every platform and thread count, handed to any renderer as
data, and an optional GPU part that runs them through Maul RHI.

### Added

- Contexts that take the program's allocator, task hooks and limits;
  effects compiled once from defs; instances that play them at a
  place, with a seed, a quality level and an off-screen policy; steps
  in ticks of 2^-20 seconds whose results do not depend on how time
  is split.
- Emission by rate and rate curve, bursts, distance travelled, other
  emitters' events and the program's own requests, from points,
  lines, circles, spheres, hemispheres, boxes, cones and meshes.
- Motion integrated exactly under gravity and drag; wind, attractors,
  vortices, curl-noise turbulence (analytic or baked into a grid),
  vector fields and a gas's velocity; velocity modules, values over
  life, kill rules, custom attributes, instance parameters and the
  program's own modules.
- Collision with planes, spheres, boxes, heightfields and distance
  grids along each step's whole path, with the program's scene through
  queries it answers, and with the camera's depth image.
- Neighbor grids, flocking and steering, pair forces, contacts and
  plexus links; ropes, chains and cloth by XPBD that pin, tear, take
  the wind and keep off themselves; rigid spheres, capsules, boxes and
  convex hulls that tumble, stack and sleep.
- Smoke and fire on grids, liquids by position-based fluids or APIC,
  surfaces of particles meshed by marching cubes, and water surfaces
  of waves or shallow water that particles float on, push and pour
  into.
- Output in the program's own layouts: particle records sorted
  stably, quads, ribbons, tubes, beams, plexus strips, lights, sheet
  meshes, surface meshes, gas and water fields and samples, bounds and
  draw order.
- Events, sub-emitters, quality levels and a particle budget.
- Effect files, read as hostile input with the offset of any fault,
  and a JSON form for diffs and authoring (`tools/effect_json.py`).
- The GPU part, `maul-vfx-rhi`: planned instances simulated in WGSL
  kernels on Vulkan, Direct3D 12, Metal and WebGPU through Maul RHI,
  equal to the CPU path in every count and within stated tolerances
  in its floats, with every output written on the device.
- Support for Linux (x86-64 and arm64), macOS, Windows and the web.
