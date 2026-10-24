# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.1.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.0.1...v0.1.0) (2026-10-24)


### ⚠ BREAKING CHANGES

* **api:** `mvfxCreateEffectFromBytes` now counts one misuse (`mvfxGetContextMisuse`) for each invalid argument of the program's own, where it counted none: `bytes` NULL with `size` above 0, a negative `size`, `assets` NULL with `assetCount` above 0, a negative `assetCount`, and an asset table entry of an unknown kind, with a NULL name, a name of 0 bytes or over `MVFX_MAX_ASSET_NAME` bytes, a name that is not UTF-8, or a name given twice in one kind. A file refused for its contents still counts none.
* **api:** `mvfxMesh_HullCenter` takes a new `bool* hullOut` parameter before `centerOut`, and returns `mvfx_success` with `hullOut` false for a mesh with no hull, where it returned `mvfx_errorInvalid`. `mvfxMesh_HullVertexCount` returns `mvfx_success` with a count of 0 for such a mesh. Neither counts a misuse for a mesh's vertices, and `mvfxCreateEffectFromBytes` counts none for a file's contents. `mvfxCreateEffect` counts no misuse for a rigid hull over a mesh with no hull.
* **gpu:** `mvfxGpuDef` has a new `version` field after its cookie, and `mvfxDefaultGpuDef` is now a `static inline` function in `maul-vfx-rhi/gpu.h` instead of an exported symbol. A program built with the 0.0.1 headers must be rebuilt; `mvfxCreateGpu` refuses a def built against other headers with `mvfx_errorVersion`.

### Fixed

* **api:** count misuse for bad arguments to mvfxCreateEffectFromBytes ([#18](https://github.com/Rawframe-Project/maul-vfx/issues/18)) ([a909e7f](https://github.com/Rawframe-Project/maul-vfx/commit/a909e7f846ccbf10327c61cececec7d3fec4aaf0))
* **api:** count no misuse for bad untrusted data ([#14](https://github.com/Rawframe-Project/maul-vfx/issues/14)) ([447d75d](https://github.com/Rawframe-Project/maul-vfx/commit/447d75dd528260d00dfc876d30ba91fb2c5ebbfc))
* **gpu:** stamp the GPU part's root def with the ABI version ([#13](https://github.com/Rawframe-Project/maul-vfx/issues/13)) ([99cf76e](https://github.com/Rawframe-Project/maul-vfx/commit/99cf76e51da03872a87818ff6d9a8775236be57e))

## [0.0.1](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.0.1) (2026-10-23)

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
