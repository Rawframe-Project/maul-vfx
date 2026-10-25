# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.3.2](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.3.2) (2026-10-25)

### Fixed

- `mvfxCreateGpuSim` no longer builds the 2 MB simulation struct as a
  temporary. An unoptimized build put that temporary on the stack, and
  its 2,160,296-byte frame overflowed a Windows thread's default 1 MB
  stack, so every call crashed in a Debug build. Optimized builds were
  not affected. Results, the effect file and the GPU part's tolerances
  are unchanged.
  ([#63](https://github.com/Rawframe-Project/maul-vfx/pull/63))

## [0.3.1](https://github.com/Rawframe-Project/maul-vfx/compare/v0.3.0...v0.3.1) (2026-10-25)

### Fixed

- The GPU part's kernels are compiled with naga's Restrict index policy:
  an index into an array, a buffer or a texture is held in range, so a
  kernel's result is defined on every driver for any input, as the
  core's is. The kernel containers' headers grow by 5.9% (74,257,784 to
  78,646,288 bytes). Results, the effect file, the CPU path and the GPU
  part's tolerances are unchanged
  ([#60](https://github.com/Rawframe-Project/maul-vfx/pull/60)).

## [0.3.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.2.0...v0.3.0) (2026-10-25)

### Changed

- **Breaking:** every value a source draws is now finite. The new
  `MVFX_MAX_DRAW` (1e38, `value.h`) bounds it: a source's base at its
  largest size times the larger size of its ends must be at most that.
  The base is a curve's largest baked value (a cubic's rise between keys
  counts, not only its keys), a gradient's largest colour or alpha key,
  or a parameter's value. Refused:
  - an effect with a curve or gradient source past the bound, made from
    a def or read from an effect file;
  - an effect whose parameter defaults take a source reading them past
    it, each component checked against the ends of the sources that
    read it;
  - a value set with `mvfxInstance_SetParameter` that does the same,
    with `mvfx_errorInvalid`, counted as misuse; a component no source
    reads takes any finite value.

  Before, a curve key of 1e18 between ends of 1e30 drew an infinite
  value. The effect file's bytes and version are unchanged, and so are
  the simulation's results for every effect still taken. Values can
  still become infinite after a birth, through the over-life products (a
  size times its size over life, a colour times its colour over life, a
  rotation speed times its scale), which
  [#53](https://github.com/Rawframe-Project/maul-vfx/issues/53) tracks
  ([#55](https://github.com/Rawframe-Project/maul-vfx/pull/55)).

### Fixed

- The GPU part gives a NaN the value the CPU path gives wherever a
  kernel turns a float into an integer (a lattice cell, a sample count,
  a curve's sample, a capsule's core spans, a contact's tick). It holds
  the float as the core's `fminf(fmaxf(v, low), high)` does, taking a
  NaN to `low`, where before the integer depended on the driver. Every
  finite value converts as before, so results and the GPU part's
  tolerances are unchanged
  ([#54](https://github.com/Rawframe-Project/maul-vfx/pull/54)).

## [0.2.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.1.2...v0.2.0) (2026-10-25)

### Changed

- **Breaking:** the GPU part (`maul-vfx-rhi`) now builds on Maul RHI
  0.3.0 instead of 0.0.1. Its public headers include Maul RHI's, and a
  host hands it its own Maul RHI device, so a host that uses Maul RHI
  itself moves to 0.3.0 too. A Maul RHI driver built outside Maul RHI's
  tree must be rebuilt for its driver interface version 7. What a host
  gets from Maul RHI 0.1.0 to 0.3.0:
  - on Direct3D 12, the GPU part's transfer passes (uploads, clears and
    copies, all of buffers) run on a copy queue of their own, each after
    the passes it depends on; a device also takes more memory from the
    host's allocator when it is made;
  - a buffer read in a second read state takes the barrier it was
    missing, which can raise a frame's barrier count.

  The core is unchanged, and so are the simulation's results, the effect
  file format and the GPU part's tolerances
  ([#49](https://github.com/Rawframe-Project/maul-vfx/pull/49)).

### Fixed

- A planned instance's births that the host spawns before a looping
  emitter's start delay ends now report a `sourceAge` of 0 in their plan
  (`mvfxPlannedBirth`), the loop's start, instead of a negative share of
  the loop. What those births draw is unchanged
  ([#50](https://github.com/Rawframe-Project/maul-vfx/pull/50)).

## [0.1.2](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.1.2) (2026-10-24)

### Changed

- The allocator's `alloc` and `free` and the task hooks' `enqueueTask`
  and `finishTask` now run in the host thread's own floating-point
  setting, even when the library calls them from inside a step or an
  effect's creation. Called from there, they used to run in the
  library's. Host modules still run in the library's setting, since
  their results feed the simulation.

### Fixed

- Every public call that computes floats now runs in the library's
  floating-point setting (round to nearest, subnormals kept, no
  traps). It leaves the thread's setting as it found it, sticky
  exception flags included, and on 32-bit x86 the x87 control word too.
  Until 0.1.1 only a step, a prewarm, an effect's compile and the task
  ranges did this. Every other call ran in the host thread's setting,
  so a host with flush-to-zero, denormals-are-zero or another rounding
  mode could get different bits, and the call's arithmetic could leave
  exception flags behind. The same bits under any host setting now
  hold for:
  - effects: `mvfxCreateEffect`, `mvfxCreateEffectFromBytes`,
    `mvfxReadEffect`, `mvfxWriteEffect`, `mvfxEffect_GetEmitterView`;
  - meshes: `mvfxCreateMesh`, `mvfxMesh_SetPositions`,
    `mvfxMesh_HullCenter`, `mvfxMesh_HullVertexCount`;
  - instances: `mvfxCreateInstance`, `mvfxInstance_SetTransform`,
    `mvfxInstance_SetColliders`, `mvfxInstance_SetAttachFrames`,
    `mvfxInstance_SetDepthImage`, `mvfxInstance_WakeBodies`,
    `mvfxInstance_PushWater`, `mvfxInstance_AnswerSceneQueries`,
    `mvfxInstance_Stop`, `mvfxInstance_Restart`,
    `mvfxInstance_GetEmitterStats`;
  - steps: `mvfxContext_Step`, `mvfxContext_StepTicks`,
    `mvfxInstance_Prewarm`;
  - output: `mvfxInstance_WriteParticles`, `mvfxInstance_WriteLights`,
    `mvfxInstance_WriteRibbons`, `mvfxInstance_WritePlexus`,
    `mvfxInstance_WriteSurfaceMesh`, `mvfxInstance_ExpandSprites`,
    `mvfxInstance_ExpandRibbons`, `mvfxInstance_ExpandTubes`,
    `mvfxInstance_ExpandSheets`, `mvfxInstance_ExpandPlexus`,
    `mvfxInstance_GetBounds`, `mvfxContext_OrderEmitters`;
  - gases and water: `mvfxInstance_ReadGas`, `mvfxInstance_ReadGasAs`,
    `mvfxInstance_SampleGas`, `mvfxInstance_GetGasInfo`,
    `mvfxInstance_ReadWater`, `mvfxInstance_SampleWater`,
    `mvfxInstance_GetWaterInfo`;
  - planned instances: `mvfxInstance_GetEmitterPlan`,
    `mvfxInstance_GetEmitterStepPlan`, `mvfxInstance_GetGasPlan`,
    `mvfxInstance_GetGasStepPlan`, `mvfxInstance_GetWaterPlan`,
    `mvfxInstance_GetWaterStepPlan`, `mvfxInstance_SetLiquidSpeed`,
    `mvfxInstance_SetWaterSpeed`, `mvfxInstance_SetWaterCounts`;
  - without a context: `mvfxSrgbSteps`, `mvfxBakeCurlNoise`.

  The other public calls compute no float and need no change:
  defaults, counts, views of stored values, destruction, integer draws,
  grids' values (converted to binary16 by their bits) and inputs stored
  as given.

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
