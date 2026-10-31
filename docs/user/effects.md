# Effects

An effect is a def compiled once by `mvfxCreateEffect`: its emitters, gases and water surfaces, its instance parameters and its quality levels. It never changes after; instances play it. The def's arrays may be released once the effect is made, since the effect copies and bakes what it needs. Every field is checked when the effect is made, so a def that is accepted steps without further checks. `maul-vfx/effect.h` documents each field with its range.

## Emitters

An emitter spawns particles and decides how they live. Start from `mvfxDefaultEmitterDef`, which spawns 64 particles a second into a pool of 1,024 in world space, and set what the effect needs.

**Capacity.** `capacity` is the most particles the emitter holds at once. A birth past it is dropped and counted (`mvfxEmitterStats.birthsDropped`), never stored somewhere else.

**Spawning.** The births an emitter owes come from its `rate` (shaped over a loop by `rateCurve`), its `bursts` (a count drawn per firing, cycles, an interval and a probability), the distance its instance moved (`ratePerDistance`), another emitter's events ([events](events.md)) and the program's own requests (`mvfxInstance_Spawn`, up to `hostSpawnCapacity` between steps). `startDelay`, `duration` and `loops` set when it spawns: no duration spawns without end. Each birth happens at its exact tick, whatever the steps, and moves for the rest of its step from there.

**Space.** `space` keeps particles in the world (`mvfx_spaceWorld`), where they stay as the instance moves, or in the instance's space (`mvfx_spaceLocal`), where they move with it. A world-space emitter may give its particles a share of the instance's velocity (`inheritVelocity`).

**Where they are born.** `shape` is a point, line, circle, sphere, hemisphere, box, cone or one of the context's meshes (vertices, edges, surface or volume), with an offset, a rotation and a thickness from its surface inward. Points are uniform over the shape by length, area or volume. `velocity` sets off each particle along the shape's normal, away from its center, within a cone or in a fixed direction, blended toward a random direction, at a speed drawn from a range.

**Initial values.** `lifetime` is required. `size`, `color`, `angle` and `spin`, `orientation` and `angularVelocity`, the flipbook `frame`, the `meshIndex` and up to `MVFX_MAX_CUSTOM_ATTRIBUTES` custom attributes are stored only when their source is set, so an emitter pays only for what it uses. Every source (`mvfxScalarSource`, `mvfxVectorSource`) is a base times a uniform draw between a minimum and a maximum: the base is 1, a curve or a gradient over the emitter's age (the share of its loop with a duration, the loop's start for a birth before `startDelay` ends; else seconds since it began), a gradient at a random point, or an instance parameter. An initial value draws its source once, at the particle's birth.

**Curves and gradients.** Curves are keys with constant, linear or cubic segments; gradients are colour and alpha keys. The effect bakes both into fixed tables when it is made, so its keys may be released then.

## Motion and forces

`motion` sets a constant `gravity` (in the world's or the emitter's space), linear `drag`, an optional per-particle drag scale and angular drag. The integrator moves particles exactly under constant acceleration and drag over each step, so a particle lands at the same place whatever the step length. Each step then holds a moved particle's speed within `MVFX_MAX_SPEED` (1e9 of the emitter's units a second, times the instance's scale for a world-space emitter), scaling a faster velocity down to it so its heading is kept, so forces and an inherited instance velocity build a speed up only to there. A rigid body's speed is held the same way at the end of its step, and a gas's velocity at each face ([gases](fluids.md#gases)). Units and a particle liquid keep their velocities apart.

`forces` apply in order: a constant acceleration, wind (drag toward a wind velocity), an attractor (with a kill radius), a vortex, curl-noise turbulence, a vector field sampled from one of the context's grids, or a gas's velocity. Each may fall off with distance within a sphere, a tube or a cone.

A force's `strength` is a scalar source as well, but it is resolved per instance per step, not per particle, and never stored. A range draws once per instance and force, from the instance's seed and the force's index, and stays the same for every step. A curve reads the emitter's loop age at the step's start (its seconds since it began without a duration; a gas's force reads the instance's seconds). A parameter reads the instance's value at each step, so `mvfxInstance_SetParameter` changes the strength from the next step on. Turbulence is analytic, or baked into a periodic grid by `mvfxBakeCurlNoise` and read as a vector field, which is cheaper for many particles.

`velocityModules` act on top of the motion: scale a particle's own motion over life, add a velocity, orbit about an axis, move away from or toward a point, and limit the speed with a smooth loss that holds whatever the steps.

## Values over life

`overLife` sets what particles show as they age: colour and alpha from gradients, size from curves, the flipbook frame (still, at a rate, or over life) and rotation speed. These values are computed where they are read (output, events, ribbons) from the stored initial values and the particle's age and speed, and never written back.

## Ending particles early

`killRules` end particles before their life ends: past a plane, inside or outside a sphere or a box, slower than a speed, or after a number of collisions. Colliders may also kill, and so may an attractor's kill radius. A killed particle writes a death event like any other.

## Parameters

An effect's `parameters` are four floats each, with defaults in the def. `mvfxInstance_SetParameter` changes them per instance, and a value source of the parameter kind reads them at a birth, or at each step for a force's strength. Births from the next step read the new value; particles already alive keep what they drew. Parameters let one effect take a colour, a size or a direction from the game.

## Limits of what particles show

Every value a particle stores or shows stays within its role's limit, so every output (records, quads, ribbons, lights, events, bounds) is finite:

| Role | Limit | What it bounds |
|---|---|---|
| A life curve | `MVFX_MAX_LIFE_SCALE` (1e6) | Its value times its curve's largest baked sample, wherever one is held: a size, alpha or rotation speed over life, a custom attribute's scale, a velocity module, a ribbon's width, a pair force. |
| A size | `MVFX_MAX_EXTENT` (1e6) | Each axis as shown: the stored size times its size over life. |
| A colour | `MVFX_MAX_COLOR` (1e6) | Each channel as shown: the stored colour times its gradient over life, alpha also times its alpha over life. Every gradient's keys are within it too. |
| A spin or angular velocity | `MVFX_MAX_SPIN` (1e6 radians a second) | The stored value times its rotation speed over life. A sprite's angle then stays within 1e12 radians of its initial one. |
| A custom attribute | `MVFX_MAX_VALUE` (1e30) | Each component times its scale over life. |
| Anything else a source draws | `MVFX_MAX_DRAW` (1e38) | An angle, an orientation, a drag scale, a lifetime. |
| A place a host module leaves | `MVFX_MAX_PLACE` (1e30) | Each axis, in the emitter's space. The step's own places stay far within it, and times the largest instance scale it is still finite. |

So each stored value has a cap: its role's limit over the most its values over life scale it. A source's base at its largest size (a curve's largest baked value, rises between keys included, a gradient's largest key, a parameter's value) times the larger size of its ends must be within its cap. An effect with a source past it is refused, and so is a parameter default or a value set with `mvfxInstance_SetParameter` that takes a source reading it past it (`mvfx_errorLimit`). A colour or size the program spawns with (`mvfxSpawnRequest`) past its cap is refused the same way; one inherited from an event is held to the spawning emitter's cap.

## Quality levels and the particle budget

`qualityLevels` lists cheaper versions of the effect: each scales every emitter's capacity and rates, and may leave emitters out. An instance asks for a level (`mvfxInstanceDef.quality`) and plays at the first level from it whose particles fit the context's particle budget (`mvfxContextLimits.particles`), which counts the sum of the capacities of all live instances. `mvfxInstance_GetQuality` reads the level an instance got, and `mvfxContext_GetParticles` what the budget holds.

## Host modules

`hostModules` run the program's own functions over an emitter's particles, before or after the step moves them (`mvfx_stageBeforeMotion`, `mvfx_stageAfterMotion`). A module reads and writes the particle arrays in place, can kill particles, and draws random numbers from `mvfxDrawModuleRandom`, whose stream is seeded per instance and module, so the step stays deterministic if the module is. What a module leaves is checked after it runs: a particle left with a place past `MVFX_MAX_PLACE` (1e30) on an axis, an angle or orientation that is not finite, a velocity past the speed the step holds, a colour, size, spin, angular velocity or custom attribute past its cap (a rigid body's angular velocity when it both turns the body's largest size faster than 4 times the speed bound and gives it more angular momentum than the cap about every axis would, since its own step rolls a tiny body and precesses a spinning one past the cap, or when it is past 2^60 radians a second, or for a body of no volume past the cap on an axis), or a rigid sphere's or capsule's size whose axes its shape does not set differ from X ([interaction](interaction.md#rigid-particles)), is killed and counted in `valueKillCount` (before motion, at once, so the step never moves it). A module runs on whichever thread steps its instance, possibly beside itself for another instance, and calls no function that changes the context. An effect with host modules cannot run on the GPU part.

## Beyond points

An emitter's births may be more than points:

- [Ribbons, trails and beams](ribbons.md) chain an emitter's particles.
- [Interaction](interaction.md): neighbors, flocking, contacts, plexus links, ropes and cloth, rigid bodies.
- [Fluids](fluids.md): liquids, gases and water surfaces.
- [Collision](collision.md) with the effect's colliders, the program's scene and the camera's depth.

Effects can also be written as files and read back; see [effect files](effect-files.md).
