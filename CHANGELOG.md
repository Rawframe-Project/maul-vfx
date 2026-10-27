# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.6.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.5.0...v0.6.0) (2026-10-27)

### Changed

- **Breaking: the GPU part (`maul-vfx-rhi`) builds on Maul RHI 0.8.0**
  instead of 0.7.0. A host that uses Maul RHI itself moves to 0.8.0
  too: every new device texture reads as zeros in its first frame,
  cleared in Maul RHI's own zero pass, and drivers built outside Maul
  RHI's tree declare driver interface version 8. The GPU part's own
  textures are its 1-pixel depth and normal images, which no kernel
  reads, so its results are unchanged. The core, the simulation's
  results, the effect file format and the GPU part's tolerances are
  unchanged
  ([#101](https://github.com/Rawframe-Project/maul-vfx/pull/101)).
- **Breaking: `mvfxPlannedLiquid` gains `unit`, after `radius`.** Its
  `square`, `poly6`, `spiky`, `restInverse`, `softening`,
  `tensileInverse` and `tensileScale` are now in that unit rather than
  in the particles' space; see the fix below
  ([#95](https://github.com/Rawframe-Project/maul-vfx/pull/95)).
- On WebAssembly the float environment's entries compile away, so each
  public function that works with floats compiles to its worker alone:
  the web build's library is 1.4%
  smaller, with the same results. Native builds are unchanged
  ([#105](https://github.com/Rawframe-Project/maul-vfx/pull/105)).

### Fixed

- A particle liquid stays finite over its kernel radius's whole valid
  range. Before, a radius near either end, such as a world-space liquid
  under an instance's largest or smallest scale, made its kernel
  factors overflow or round to 0, and its particles' places and
  velocities went non-finite by the second step. The kernels now
  measure lengths in a unit, the power of two that leaves the radius
  from 1 to 2 in it. Every scaling is exact, so results within
  binary32's normal range are the same bits and the golden hashes are
  unchanged
  ([#95](https://github.com/Rawframe-Project/maul-vfx/pull/95)).
- The GPU part's liquid kernel moves a correction out of the unit by a
  product, exact on every device, rather than a division, which WGSL
  allows to be 2.5 ULP off
  ([#98](https://github.com/Rawframe-Project/maul-vfx/pull/98)).

## [0.5.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.4.0...v0.5.0) (2026-10-26)

### Changed

- **Breaking:** the GPU part (`maul-vfx-rhi`) now builds on Maul RHI
  0.7.0 instead of 0.3.0. Its public headers include Maul RHI's, and a
  host hands it its own Maul RHI device, so a host that uses Maul RHI
  itself moves to 0.7.0 too. What a host gets from Maul RHI 0.3.0 to
  0.7.0:
  - every new buffer reads as zeros until the frame graph writes it,
    on every backend, cleared in the first frame that imports it by a
    pass of Maul RHI's own that takes nothing from the frame's budgets;
    the buffers `mvfxGpuSim_GetParticles` hands out are therefore
    zeroed in the frame of the simulation's first step, though until
    that frame has run they stay undefined, as `sim.h` says;
  - shader containers are version 2, and version 1 is refused: the GPU
    part's kernel containers are rebuilt with 0.7.0's tools, and a
    host's own containers must be written again with them;
  - a pipeline cache is read only under the Maul RHI ABI version that
    wrote it, so caches from 0.6.0 or earlier are refused;
  - every failed call sets the pointers and ids it was given to NULL or
    the null id;
  - a program's pass ids start at 2;
  - on Direct3D 12, a transfer pass that clears runs on the compute
    queue, since copy lists cannot clear: the GPU part's contacts' and
    self-collision's write passes every step, a grid's the first time
    it empties its counts, an emitter's at the first step, and a gas's
    and a water surface's after a clear;
  - conformance mustpass lists for driver interface versions 4 to 6 are
    gone, and diagnostic codes are no longer promised to stay fixed.

  The core is unchanged, and so are the simulation's results, the
  effect file format and the GPU part's tolerances. On Direct3D 12
  (WARP) the drag factors stay at #84's measure: decay 8.01e-7, carry
  1.85e-7 and push 2.20e-7 against bounds of 4e-6, 2e-6 and 2e-6
  ([#91](https://github.com/Rawframe-Project/maul-vfx/pull/91)).
- `sim.h` and the user guide say that every buffer
  `mvfxGpuSim_GetParticles` gives is undefined until the simulation's
  first step is recorded and its frame has run
  ([#89](https://github.com/Rawframe-Project/maul-vfx/pull/89)).

### Fixed

- The GPU sample builds on Windows with warnings as errors again: it
  defines `_CRT_SECURE_NO_WARNINGS` for its `getenv`, as the tests do.
  The nightly Direct3D 12 job had stopped at its build since #69, in
  0.4.0 ([#92](https://github.com/Rawframe-Project/maul-vfx/pull/92)).

## [0.4.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.3.2...v0.4.0) (2026-10-26)

### Changed

- **Breaking:** every value a particle stores or shows now stays within
  a limit for its role, so every output (records, sprites, ribbons,
  lights, events, bounds) is finite for an effect without a solver of
  its own. Before, the products formed after a birth were unbounded: a
  size, alpha or rotation speed over life could reach 1e48, any space
  scale past 3.4 made a size drawn near `MVFX_MAX_DRAW` infinite, and a
  host module could leave a NaN. A limit applies where the value comes
  from:
  - **Refused** (`mvfx_errorInvalid`): an effect whose life curve's
    value times its largest baked sample passes the new
    `MVFX_MAX_LIFE_SCALE` (1e6), whose gradient key passes the new
    `MVFX_MAX_COLOR` (1e6), or whose value source can draw past its cap:
    a size's axis past `MVFX_MAX_EXTENT` over its size over life, a
    colour's channel past `MVFX_MAX_COLOR` over its colour over life, a
    spin or angular velocity past the new `MVFX_MAX_SPIN` (1e6 rad/s)
    over its rotation speed over life, a custom attribute past
    `MVFX_MAX_VALUE` over its scale, anything else past
    `MVFX_MAX_DRAW`. So are a parameter default or a value set with
    `mvfxInstance_SetParameter` that takes a source reading it past its
    cap, and a `mvfxSpawnRequest` colour or size past its emitter's cap.
  - **Held:** an event's inherited colour and size, to the spawning
    emitter's caps; each moved particle's velocity, within the new
    `MVFX_MAX_SPEED` (1e9) per axis, times the instance's scale for a
    world-space emitter.
  - **Killed:** a particle a host module leaves with a place past the
    new `MVFX_MAX_PLACE` (1e30) on an axis, a non-finite angle or
    orientation, a velocity past the hold or a value past its cap. It
    is counted in the new `mvfxEmitterStats.valuesKilled`, apart from
    `killed`.

  `mvfxOverLifeView` gains `colorCaps` and `sizeCaps`, so a host's own
  GPU path can hold inherited values as the library does. Every effect
  in the samples, the tests and the user guide is taken unchanged, and
  the largest value any of them uses, apart from the tests written at
  the limits, is at least 1,500 times below its limit. The check after
  a host module costs under 1 ns a particle a step (measured on 100,000
  sprites, one core, Release). The simulation's results for every
  effect still taken, the effect file's bytes and version, and the GPU
  part's tolerances are unchanged. Solvers (liquids, rigid particles,
  contacts and units, gases, water surfaces) keep their own state and
  are not yet bounded this way
  ([#79](https://github.com/Rawframe-Project/maul-vfx/pull/79)).

### Fixed

- The GPU part's drag factors no longer lose precision in binary32 just
  past their series cutoff. A birth's move, a particle with its own drag
  scale, a particle under winds with a falloff and a particle in water
  took a push up to 16.9 times off its value; the push is now within
  3.6e-7 of the core's (relative), and every factor within 9e-7. GPU
  results change within the GPU part's tolerances, which do not change.
  The CPU path, its results and the effect file are unchanged
  ([#84](https://github.com/Rawframe-Project/maul-vfx/pull/84)).
- An emitter whose speed limit is off no longer carries that limit's
  dampen into its step, so a NaN there no longer reaches the binary64
  exponential's conversion, which sanitizers reported as undefined
  behaviour; the binary64 exponential also gives a NaN back before
  converting. No output changed; results are unchanged
  ([#82](https://github.com/Rawframe-Project/maul-vfx/pull/82)).

### Security

- The GPU part (`MAUL_VFX_RHI=ON`) now writes every byte of the buffers
  `mvfxGpuSim_GetParticles` hands to the host at a simulation's first
  step, before any of them holds a step's results. After a clear and at
  a simulation's first step, the whole statistics record is uploaded,
  with every word the emitter does not keep at 0. At that first step
  the two particle buffers, the events buffer and the plexus links
  buffer are cleared. `sim.h` states that unkept statistics words hold
  0, and `sim.h` and the user guide state that slots and entries past
  each count are undefined and not to be read. Results, the effect file
  and the GPU part's tolerances are unchanged
  ([#86](https://github.com/Rawframe-Project/maul-vfx/pull/86),
  [#87](https://github.com/Rawframe-Project/maul-vfx/pull/87),
  [GHSA-hj35-99h7-jf52](https://github.com/Rawframe-Project/maul-vfx/security/advisories/GHSA-hj35-99h7-jf52)).

## [0.3.2](https://github.com/Rawframe-Project/maul-vfx/compare/v0.3.1...v0.3.2) (2026-10-25)

### Fixed

- `mvfxCreateGpuSim` no longer builds the 2 MB simulation struct as a
  temporary. An unoptimized build put that temporary on the stack,
  where its 2,160,296-byte frame overflowed a Windows thread's default
  1 MB stack on every call. Optimized builds were not affected.
  Results, the effect file and the GPU part's tolerances are unchanged
  ([#63](https://github.com/Rawframe-Project/maul-vfx/pull/63)).

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
