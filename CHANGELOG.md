# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.7.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.6.0...v0.7.0) (2026-11-01)

This release changes the simulation's results: rigid bodies turn by a
new flow, rigid spheres' and capsules' unused sizes are tied to X, the
coupled water's lift rounds differently, and spawn draws take unbiased
bits, so the golden hashes move. The effect file format rises from
version 1 to 4 (files of earlier versions are refused), and the public
API takes the family's names, types and results throughout.

### Added

- A refused effect names the field at fault.
  `mvfxCreateEffect`, `mvfxWriteEffect`, `mvfxReadEffect` and
  `mvfxCreateEffectFromBytes` take an optional last parameter,
  `mvfxEffectRefusal*`, which holds the status, the refused field's path
  of file tags with the item of each array, and the emitter, gas or
  water surface it is in. `mvfxGetEffectFieldPath` writes that path as
  text, such as `emitters[2].overLife.size.curve.keys[3].time`. A def an
  effect file holds that the validator refuses is now reported at the
  refused field's offset, not at the effect section's
  ([#182](https://github.com/Rawframe-Project/maul-vfx/pull/182)).
- A force's strength is a value source (`mvfxScalarSource`), resolved
  per instance per step: a range draws once per instance and force, a
  curve reads the emitter's loop age at the step's start, and a
  parameter reads its x at each step. An effect can now ramp a force over
  its loop or give each instance its own strength without driving a
  parameter every frame. `strengthFromParameter` and `parameter` are
  removed; a parameter source says the same. A strength of one value
  resolves bit for bit as the old float did. The golden scene now drives
  its forces by curves and a range, so its hashes change
  ([#184](https://github.com/Rawframe-Project/maul-vfx/pull/184)).

### Changed

- **Breaking: the effect file is written as tagged fields and leaves
  defaults out.** Each field carries a 32-bit tag and its length, a
  reader gives an absent field its default and refuses a tag it does not
  know, and the writer leaves out every field that holds its default, so
  a field added to a def no longer changes every file's bytes. The
  golden scene's smallest effect goes from 2,492 bytes to 1,704. With
  #184 and #206
  below, `MVFX_FILE_VERSION` goes from 1 to 4 and the effect JSON form
  from format 1 to 5; files of earlier versions are refused with
  `mvfx_errorVersion`. Results are unchanged by the format itself
  ([#180](https://github.com/Rawframe-Project/maul-vfx/pull/180),
  [#184](https://github.com/Rawframe-Project/maul-vfx/pull/184),
  [#206](https://github.com/Rawframe-Project/maul-vfx/pull/206)).
- **Breaking: results take the family's shared values and meanings.**
  Every error result but invalid and capacity takes the family's value
  (stale -3, state -6, version -7, limit -9, busy -10, missing -32), and
  the new `mvfx_errorUnsupported` (-4) and `mvfx_errorFormat` (-8) are
  returned. A call from the context's host code answers
  `mvfx_errorBusy` instead of `mvfx_errorState`; a call needing a
  planned or an unplanned instance, or an effect the GPU part does not
  run, answers `mvfx_errorUnsupported`; a creation past a table of the
  context's def answers `mvfx_errorCapacity` instead of
  `mvfx_errorLimit`; malformed effect file bytes answer
  `mvfx_errorFormat` instead of `mvfx_errorInvalid`.
  `mvfxPlannedBirth`'s `reserved[2]` are `eventKeyLow` and
  `eventKeyHigh`
  ([#123](https://github.com/Rawframe-Project/maul-vfx/pull/123)).
- **Breaking: a value past one of the library's named bounds
  (`MVFX_MAX_*`, `MVFX_MIN_*`) is refused with `mvfx_errorLimit`**, where
  it answered `mvfx_errorInvalid`, in defs and direct arguments alike,
  and a def past a bound counts no misuse. NaN, infinities and ranges
  without a named constant still answer `mvfx_errorInvalid`. Every def
  and argument accepted before is accepted now
  ([#145](https://github.com/Rawframe-Project/maul-vfx/pull/145)).
- **Breaking: public names follow the family's naming rule.** Getters
  take `Get` (`mvfxResultName` is `mvfxGetResultName`,
  `mvfxGetContextMisuse` is `mvfxContext_GetMisuse`), hook types end in
  `Function` (`mvfxTaskFn` is `mvfxTaskFunction`), switch fields take
  `enable` (`enabled` is `enable`, `sceneQueries` is
  `enableSceneQueries`), and outputs end in `Out`; the pull request lists
  every rename. A counter is named by what it counts (`...Count`,
  `...Dropped`, `...Refused`) in `mvfxEmitterStats`, `mvfxEmitterPlan`,
  `mvfxWaterInfo` and the GPU part's statistics words.
  `mvfxRangeSource` is `mvfxMakeRangeSource`, the GPU part's
  `mvfxGpuSim_Expand*` calls are `mvfxGpuSim_Add*Expansion`, and
  `mvfxContext_Step`, `mvfxContext_StepTicks` and
  `MVFX_MAX_STEP_SECONDS` move to `maul-vfx/context.h`. Four reads take
  a `const mvfxContext*` and are not refused as busy
  ([#179](https://github.com/Rawframe-Project/maul-vfx/pull/179),
  [#212](https://github.com/Rawframe-Project/maul-vfx/pull/212),
  [#215](https://github.com/Rawframe-Project/maul-vfx/pull/215)).
- **Breaking: every kind is an `int32_t` and every flag set a
  `uint32_t`.** The 45 kind typedefs were `uint8_t`, so a kind kept in an
  `int` was truncated instead of refused: -1 became 255, and
  `INT32_MIN` became 0, a valid kind. `mvfxGasWalls` and
  `mvfxWaterWalls` are `uint32_t`, the event masks are the new
  `mvfxEventMask`, and the raw `space` fields are `mvfxSimulationSpace`,
  which moves to `modules.h`. Struct layouts change, and a negative kind
  is refused everywhere
  ([#206](https://github.com/Rawframe-Project/maul-vfx/pull/206)).
- **Breaking: `plan.h`'s layouts are final for 1.0.**
  `mvfxPlannedKillRule.minimumAgeTicks` is `int64_t`, and
  `mvfxWaterPlan.dt` is `mvfxWaterPlan.substepSeconds`
  ([#216](https://github.com/Rawframe-Project/maul-vfx/pull/216)).
- **Breaking: a mesh's hull is built once, when the mesh is created.**
  `mvfxMesh_GetHullCenter` and `mvfxMesh_GetHullVertexCount` take a
  `const mvfxContext*`, no longer fail with `mvfx_errorCapacity` or count
  misuse, and give the hull of the positions the mesh was created with.
  A rigid hull no longer follows a `mvfxMesh_SetPositions` done before
  the effect is made
  ([#191](https://github.com/Rawframe-Project/maul-vfx/pull/191)).
- **Breaking: a rigid hull over a mesh with no hull** (vertices that span
  no volume) is refused with `mvfx_errorUnsupported`, not
  `mvfx_errorInvalid`
  ([#223](https://github.com/Rawframe-Project/maul-vfx/pull/223)).
- **Breaking: a refused output call clears its outputs.** It zeroes the
  program's buffers over the span it is given, except layout records
  with a NULL context or a stale layout, and gives counts, sizes and
  bounds of 0 even when the context or another output is NULL. A call
  refused as busy from host code writes none of its outputs
  ([#173](https://github.com/Rawframe-Project/maul-vfx/pull/173)).
- **Breaking: `mvfxCreateEffect` and `mvfxCreateEffectFromBytes` give the
  null id when the context is NULL**, instead of leaving the id as it
  was
  ([#172](https://github.com/Rawframe-Project/maul-vfx/pull/172)).
- **Breaking: `mvfxHash64` takes its byte count as `int64_t`**, so one
  call hashes 2 GiB or more. Its result is unchanged for every count
  that fits in `int32_t`
  ([#186](https://github.com/Rawframe-Project/maul-vfx/pull/186)).
- **Breaking: the GPU part (`maul-vfx-rhi`) builds on Maul RHI 0.15.0**
  instead of 0.8.0, so a host that uses Maul RHI itself moves with it.
  A host makes the device and passes it in, calls Maul RHI by its 0.12.0
  names (`mrhiPass_Begin`, `mrhiDevice_CompileFrame`,
  `mrhiRequest_TakeReadback` and the rest) and sets its device's
  `budgets`, as the guide shows. Maul RHI's enumerations are `int32_t`,
  its bit sets `uint32_t`, `mrhiPass_WriteBuffer` takes a `size_t`
  count, `mrhiResourcePlan.usage` is `bufferUsage` and `textureUsage`,
  and `mrhiQuerySetDef.type` is `kind`. A device's refusal of a call
  from inside its own allocator answers `mvfx_errorBusy`, and a creation
  past a device limit within the adapter's answers
  `mvfx_errorCapacity` where it answered `mvfx_errorUnsupported`. On
  D3D12 the kernels' float math now runs as written (DXC no longer
  reorders it), so results there can differ from those of builds
  against Maul RHI 0.10.0
  ([#138](https://github.com/Rawframe-Project/maul-vfx/pull/138),
  [#166](https://github.com/Rawframe-Project/maul-vfx/pull/166),
  [#210](https://github.com/Rawframe-Project/maul-vfx/pull/210),
  [#214](https://github.com/Rawframe-Project/maul-vfx/pull/214)).
- **Breaking: `mvfxCreateGpu` and `mvfxCreateGpuSim` answer the device's
  status when it refuses to make an object** (`mvfx_errorCapacity`,
  `mvfx_errorUnsupported`, `mvfx_errorState` for a device not ready yet,
  or the new `mvfx_errorPlatform`) instead of `mvfx_errorState`, which a
  host retried for ever; `mvfxCreateGpuSim` answers
  `mvfx_errorCapacity` when its allocator fails inside a part
  ([#125](https://github.com/Rawframe-Project/maul-vfx/pull/125)).
- **Breaking: a gas def whose `cellSize` is below `MVFX_MIN_GAS_CELL`
  (1e-6) is refused** with `mvfx_errorLimit`; see the gas fix below
  ([#149](https://github.com/Rawframe-Project/maul-vfx/pull/149)).

### Fixed

- **Breaking, results change: rigid bodies turn by a Casimir splitting
  of their free flow**, their spin and orientation together, on both
  paths. The old explicit gyroscopic step froze needles (93 of 504 in a
  probe) and drifted from the true motion: a box spun near a principal
  axis had its world momentum off by up to 1.8% and its energy by 19%
  over 10 s. Every spinning body that is not of one moment about every
  axis moves differently, and angular drag slows a body's spin before it
  turns. The golden hashes change
  ([#120](https://github.com/Rawframe-Project/maul-vfx/pull/120)).
- **Breaking, results change: a rigid sphere's Y and Z and a capsule's Z
  are held at X.** Those axes meant nothing to the solver, yet any value
  was stored, so a host drew a sphere of size {1, 3, 2} as an ellipsoid
  and the GPU part took a sphere of size {1e-20, 1e6, 1e6} as a point.
  An effect, effect file or spawn request that sets them unlike X is
  refused with `mvfx_errorInvalid`, a host module that writes such a
  size has its particle killed (counted in `valuesKilled`), and a drawn,
  a parameter's or an inherited size takes X's value there. The golden
  hashes change
  ([#109](https://github.com/Rawframe-Project/maul-vfx/pull/109)).
- **Results change: a box's surface and shell births and every speed
  drawn from a range take unbiased bits.** A burst of 40,000 particles on
  a box's surface put 75% on the negative faces, and speeds fell in
  alternate bands of their range. Both paths change alike, so a planned
  instance still matches its CPU twin. The golden hashes change, and the
  GPU part's flocking test scenes take their own tolerances (1e-3 in a
  view cone, 2.1e-4 in a full view, from 1e-4), since a close
  neighbor's separation magnifies the paths' rounding there
  ([#240](https://github.com/Rawframe-Project/maul-vfx/pull/240)).
- **Breaking, results change: water heights and depths are held within
  the new `MVFX_MAX_WATER_DEPTH`** (1e12 of its lengths) after each
  step's pushes, crossings, pours and sources. Four inputs the library
  accepts overflowed a depth or a normal. A coupled particle's
  lift is computed in another order and rounds differently, so the
  golden scene's coupled water changes from its 14th step
  ([#178](https://github.com/Rawframe-Project/maul-vfx/pull/178)).
- Rigid bodies stay finite at the size, density and scale ends the
  validator accepts. A body is solved in its own units, a pair in the
  lighter body's, and a needle's inverse inertia is held at 2^40 in its
  units. In local probes of the cases the validator accepts, 90 of 396
  bodies went non-finite and 69 more lost their spin, and 48 of 504
  needles went non-finite; now all of them stay finite and keep their
  spin, and with the new flow above no needle stops turning. Results
  within binary32's normal range are the same bits, except for needles
  thinner than about a millionth of their length
  ([#106](https://github.com/Rawframe-Project/maul-vfx/pull/106),
  [#110](https://github.com/Rawframe-Project/maul-vfx/pull/110),
  [#113](https://github.com/Rawframe-Project/maul-vfx/pull/113)).
- A rigid body's velocity is held to `MVFX_MAX_SPEED` after its own
  contacts, and its spin is checked after a host module against what its
  own step gives it, so a host module that never wrote a body's velocity
  or spin no longer gets it killed
  ([#128](https://github.com/Rawframe-Project/maul-vfx/pull/128),
  [#130](https://github.com/Rawframe-Project/maul-vfx/pull/130)).
- **Breaking: a fast particle's speed is held along its heading.** The
  step clamped each axis apart, so a particle at (2e9, 1e9) under a bound
  of 1e9 turned from 26.6 to 45 degrees. It now scales the velocity, and
  the host modules' check kills a speed past the bound rather than a
  component past it. Results change only for particles past the bound
  off an axis
  ([#135](https://github.com/Rawframe-Project/maul-vfx/pull/135)).
- `mvfxSinCos` reduces an argument past 2^24 exactly, where it gave
  sin 0 and cos 1: a body turning more than 2^24 radians in half a
  substep never turned. Results up to 2^24 are unchanged
  ([#133](https://github.com/Rawframe-Project/maul-vfx/pull/133)).
- Units stay finite where a squared length passes
  binary32's range: at the largest spacing and scale, or with places
  past 2^63 apart, a sheet's drag, a projection and a tear's direction
  went NaN, infinite or zero. Values within range keep their bits
  ([#146](https://github.com/Rawframe-Project/maul-vfx/pull/146)).
- **Breaking: a short vector is measured scaled up**, so no square
  flushes on a device that flushes subnormals. `mvfxLengthOf`, and every
  length the core measures, changes its bits only for a vector whose
  largest component is below 2^-38 and some of whose squares were
  subnormal
  ([#159](https://github.com/Rawframe-Project/maul-vfx/pull/159)).
- **Breaking: a sheet's cross product flushes below the least normal
  alike on every device.** Each scaled edge component, product and
  difference below 2^-126 is +0 on both paths, as D3D12 does by
  specification; a triangle whose cross product becomes 0 takes no drag
  ([#154](https://github.com/Rawframe-Project/maul-vfx/pull/154)).
- A sheet's air share is decided past a sixth before its product can
  overflow, on both paths
  ([#153](https://github.com/Rawframe-Project/maul-vfx/pull/153)).
- A smoke gas's faces are held to `MVFX_MAX_SPEED`: at the forces'
  limits they went NaN within the first step. Results change only where
  a face passed 1e9
  ([#149](https://github.com/Rawframe-Project/maul-vfx/pull/149)).
- A gas cell's density, temperature and fuel are held within 2^80 in its
  buoyancy, so splats from many particles no longer make a face infinite,
  or NaN under a drag
  ([#155](https://github.com/Rawframe-Project/maul-vfx/pull/155)).
- The GPU part matches the CPU path where it did not:
  - a needle's mass no longer flushes to 0, which made it a point that
    does not turn
    ([#124](https://github.com/Rawframe-Project/maul-vfx/pull/124));
  - a NaN or infinite velocity is left to the kill rules instead of a
    clamp a device may turn finite
    ([#137](https://github.com/Rawframe-Project/maul-vfx/pull/137));
  - finiteness is tested on the bits, which a device may not fold away
    as WGSL allows it to fold compares
    ([#140](https://github.com/Rawframe-Project/maul-vfx/pull/140),
    [#141](https://github.com/Rawframe-Project/maul-vfx/pull/141));
  - every angle is reduced exactly in the kernels' sine and cosine, so
    turns match the CPU path's at any angle; GPU results change
    ([#142](https://github.com/Rawframe-Project/maul-vfx/pull/142));
  - a quotient below 2^-126 rounds as C divides, where it built a NaN or
    a wrong number
    ([#152](https://github.com/Rawframe-Project/maul-vfx/pull/152));
  - a grid cell past 2^24 is taken in integers, as the core floors it
    ([#164](https://github.com/Rawframe-Project/maul-vfx/pull/164)).
- Destroying an object frees its slot before its memory, so a host's
  `free` hook that reads the destroyed id no longer reads freed memory
  ([#194](https://github.com/Rawframe-Project/maul-vfx/pull/194)).
- `mvfxContext_GetMisuse` and `mvfxContext_GetParticles` answer 0 for a
  NULL context instead of crashing
  ([#218](https://github.com/Rawframe-Project/maul-vfx/pull/218)).
- Planning a kill rule converts its `minimumAge` to ticks only for
  `mvfx_killSlower`, the kind that reads it. A NaN or 1e30 there, which
  other kinds accept, was undefined behavior
  ([#220](https://github.com/Rawframe-Project/maul-vfx/pull/220)).
- `mvfxReadEffect` refuses memory that overlaps the file's bytes with
  `mvfx_errorInvalid`, where it laid the def over bytes it was still
  reading
  ([#226](https://github.com/Rawframe-Project/maul-vfx/pull/226)).
- An out-of-range `neighbors.source` is refused naming
  `emitters[i].neighbors.source`, not the emitters as a whole
  ([#204](https://github.com/Rawframe-Project/maul-vfx/pull/204)).

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
