# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.1.2](https://github.com/Rawframe-Project/maul-vfx/compare/v0.1.1...v0.1.2) (2026-10-24)


### Fixed

* **api:** enter the float environment in every computing public call ([#40](https://github.com/Rawframe-Project/maul-vfx/issues/40)) ([18f7faf](https://github.com/Rawframe-Project/maul-vfx/commit/18f7faf20c89a41f86b38d93fb667f945d888f43))
* **base:** move the float mode onto the family's float environment ([#38](https://github.com/Rawframe-Project/maul-vfx/issues/38)) ([c6f3d9b](https://github.com/Rawframe-Project/maul-vfx/commit/c6f3d9b023d22efd3e6b98768985862ba48a2781))

## [0.1.1](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.1.1) (2026-10-24)

### Fixed

- A context step ran the gas substeps (pours, splats, buoyancy, push,
  confinement, burning and solids) and the planning of planned gas
  instances in the host thread's floating-point mode. A host running
  with flush-to-zero, denormals-are-zero or another rounding mode could
  get different gas results. The whole step now runs in the library's
  mode, and the host's mode is restored after it.

## [0.1.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.0.1...v0.1.0) (2026-10-24)

### Changed

- **Breaking:** the GPU part's def, `mvfxGpuDef`, carries the ABI version after its cookie, as the context's def does. `mvfxDefaultGpuDef` is now built into the program from the headers it includes, no longer exported by the library. Rebuild against the 0.1.0 headers: `mvfxCreateGpu` refuses a def made with other headers with `mvfx_errorVersion`.
- **Breaking:** `mvfxMesh_HullCenter` takes a `bool* hullOut` before `centerOut`, which says whether the mesh has a hull a rigid particle can take. A mesh whose vertices span no volume, or whose hull has more than `MVFX_MAX_HULL_VERTICES` vertices, now gives `mvfx_success` with `hullOut` false and a zero center, where it gave `mvfx_errorInvalid`. `mvfxMesh_HullVertexCount` gives `mvfx_success` with a count of 0 for a mesh whose vertices span no volume.
- **Breaking:** the misuse count (`mvfxGetContextMisuse`) now counts only arguments a correct program never passes:
  - `mvfxCreateEffectFromBytes` counts nothing for a file refused for its contents, including what the file asks of the asset table's grids and meshes (a grid of the wrong shape, a mesh without the shape's mode or without a hull).
  - `mvfxCreateEffectFromBytes` counts one for each invalid argument of the program's own: NULL bytes with a size, a negative size, and a NULL or invalid asset table.
  - `mvfxCreateEffect` and the hull queries count nothing for a mesh with no hull, since its vertices are a model's data.
  - A program that asserts the count stays at zero now fails only on its own mistakes, never on a bad file or model.

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
