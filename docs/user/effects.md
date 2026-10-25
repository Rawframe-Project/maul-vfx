# Effects

An effect is a def compiled once by `mvfxCreateEffect`: its emitters, gases and water surfaces, its instance parameters and its quality levels. It never changes after; instances play it. The def's arrays may be released once the effect is made, since the effect copies and bakes what it needs. Every field is checked when the effect is made, so a def that is accepted steps without further checks. `maul-vfx/effect.h` documents each field with its range.

## Emitters

An emitter spawns particles and decides how they live. Start from `mvfxDefaultEmitterDef`, which spawns 64 particles a second into a pool of 1,024 in world space, and set what the effect needs.

**Capacity.** `capacity` is the most particles the emitter holds at once. A birth past it is refused and counted (`mvfxEmitterStats`), never stored somewhere else.

**Spawning.** The births an emitter owes come from its `rate` (shaped over a loop by `rateCurve`), its `bursts` (a count drawn per firing, cycles, an interval and a probability), the distance its instance moved (`ratePerDistance`), another emitter's events ([events](events.md)) and the program's own requests (`mvfxInstance_Spawn`, up to `hostSpawnCapacity` between steps). `startDelay`, `duration` and `loops` set when it spawns: no duration spawns without end. Each birth happens at its exact tick, whatever the steps, and moves for the rest of its step from there.

**Space.** `space` keeps particles in the world (`mvfx_spaceWorld`), where they stay as the instance moves, or in the instance's space (`mvfx_spaceLocal`), where they move with it. A world-space emitter may give its particles a share of the instance's velocity (`inheritVelocity`).

**Where they are born.** `shape` is a point, line, circle, sphere, hemisphere, box, cone or one of the context's meshes (vertices, edges, surface or volume), with an offset, a rotation and a thickness from its surface inward. Points are uniform over the shape by length, area or volume. `velocity` sets off each particle along the shape's normal, away from its center, within a cone or in a fixed direction, blended toward a random direction, at a speed drawn from a range.

**Initial values.** `lifetime` is required. `size`, `color`, `angle` and `spin`, `orientation` and `angularVelocity`, the flipbook `frame`, the `meshIndex` and up to `MVFX_MAX_CUSTOM_ATTRIBUTES` custom attributes are stored only when their source is set, so an emitter pays only for what it uses. Every source (`mvfxScalarSource`, `mvfxVectorSource`) is a base times a uniform draw between a minimum and a maximum: the base is 1, a curve or a gradient over the emitter's age (the share of its loop with a duration, the loop's start for a birth before `startDelay` ends; else seconds since it began), a gradient at a random point, or an instance parameter.

**Curves and gradients.** Curves are keys with constant, linear or cubic segments; gradients are colour and alpha keys. The effect bakes both into fixed tables when it is made, so its keys may be released then.

## Motion and forces

`motion` sets a constant `gravity` (in the world's or the emitter's space), linear `drag`, an optional per-particle drag scale and angular drag. The integrator moves particles exactly under constant acceleration and drag over each step, so a particle lands at the same place whatever the step length.

`forces` apply in order: a constant acceleration, wind (drag toward a wind velocity), an attractor (with a kill radius), a vortex, curl-noise turbulence, a vector field sampled from one of the context's grids, or a gas's velocity. Each may fall off with distance within a sphere, a tube or a cone. Turbulence is analytic, or baked into a periodic grid by `mvfxBakeCurlNoise` and read as a vector field, which is cheaper for many particles.

`velocityModules` act on top of the motion: scale a particle's own motion over life, add a velocity, orbit about an axis, move away from or toward a point, and limit the speed with a smooth loss that holds whatever the steps.

## Values over life

`overLife` sets what particles show as they age: colour and alpha from gradients, size from curves, the flipbook frame (still, at a rate, or over life) and rotation speed. These values are computed where they are read (output, events, ribbons) from the stored initial values and the particle's age and speed, and never written back.

## Ending particles early

`killRules` end particles before their life ends: past a plane, inside or outside a sphere or a box, slower than a speed, or after a number of collisions. Colliders may also kill, and so may an attractor's kill radius. A killed particle writes a death event like any other.

## Parameters

An effect's `parameters` are four floats each, with defaults in the def. `mvfxInstance_SetParameter` changes them per instance, and a value source of the parameter kind reads them at a birth. Births from the next step read the new value; particles already alive keep what they drew. Parameters let one effect take a colour, a size or a direction from the game.

Every value a source draws is finite. A source's base at its largest size (a curve's largest baked value, rises between keys included, a gradient's largest key, a parameter's value) times the larger size of its ends may be at most `MVFX_MAX_DRAW` (1e38). An effect with a curve or gradient source past it is refused, and so is a parameter default or a value set with `mvfxInstance_SetParameter` that takes a source reading it past it (`mvfx_errorInvalid`).

## Quality levels and the particle budget

`qualityLevels` lists cheaper versions of the effect: each scales every emitter's capacity and rates, and may leave emitters out. An instance asks for a level (`mvfxInstanceDef.quality`) and plays at the first level from it whose particles fit the context's particle budget (`mvfxContextLimits.particles`), which counts the sum of the capacities of all live instances. `mvfxInstance_GetQuality` reads the level an instance got, and `mvfxContext_GetParticles` what the budget holds.

## Host modules

`hostModules` run the program's own functions over an emitter's particles, before or after the step moves them (`mvfx_stageBeforeMotion`, `mvfx_stageAfterMotion`). A module reads and writes the particle arrays in place, can kill particles, and draws random numbers from `mvfxModule_Random`, whose stream is seeded per instance and module, so the step stays deterministic if the module is. A module runs on whichever thread steps its instance, possibly beside itself for another instance, and calls no function that changes the context. An effect with host modules cannot run on the GPU part.

## Beyond points

An emitter's births may be more than points:

- [Ribbons, trails and beams](ribbons.md) chain an emitter's particles.
- [Interaction](interaction.md): neighbors, flocking, contacts, plexus links, ropes and cloth, rigid bodies.
- [Fluids](fluids.md): liquids, gases and water surfaces.
- [Collision](collision.md) with the effect's colliders, the program's scene and the camera's depth.

Effects can also be written as files and read back; see [effect files](effect-files.md).
