# Troubleshooting

## A call that fails

Every fallible call says why it failed:

| Result | Meaning |
|---|---|
| `mvfx_errorInvalid` | An argument the call can never take: a null pointer, NaN or an infinity, a value out of a range its documentation gives with no named constant, a def not made by its default function. It also counts one misuse on the context. A def an effect file holds that the library refuses, for what it asks of the program's assets, counts none: a file is untrusted data. |
| `mvfx_errorCapacity` | Room is too small: a buffer the program gave (the call reports the size it needs), a table of the context's def (too many effects, instances, layouts, grids or meshes at once), or the allocator. |
| `mvfx_errorStale` | An id whose object was destroyed. |
| `mvfx_errorUnsupported` | The object was made without what the call needs: a planned instance's fields, which its simulation holds; a plan of an instance that is not planned; an effect the GPU part does not run yet. |
| `mvfx_errorPlatform` | A driver refused: the GPU part's device would not make a kernel's shader or pipeline, or a simulation's buffer, for a reason Maul RHI shares no other status for. |
| `mvfx_errorState` | The call is not allowed now, and another call changes that: a planned instance's plans split into steps, a GPU frame not open, a device not ready yet. |
| `mvfx_errorVersion` | The program was built against headers of another release, or a file is of another format version. |
| `mvfx_errorFormat` | An effect file's bytes are malformed: a magic, size or hash that does not match, a unit out of place, a count past its cap, a def the def validator refuses. The offset names the first byte at fault. |
| `mvfx_errorLimit` | An argument, or a def's field, passes a fixed bound of the library, a named constant (`MVFX_MAX_*`, `MVFX_MIN_*`): more emitters than `MVFX_MAX_EMITTERS`, a size past `MVFX_MAX_EXTENT`, a hull past `MVFX_MAX_HULL_VERTICES` or `MVFX_MAX_HULL_COUNT` vertices, a frame's GPU passes past their `MVFX_GPU_*` count. A def past one counts no misuse; a direct argument past one counts one. When a def fails several checks, the first decides which of the two it answers. |
| `mvfx_errorBusy` | The object is in use: an effect an instance plays, a grid or mesh an effect holds, or a call from the allocator, a task hook or a host module that would change the context while it runs them. The GPU part answers it too when its device refuses a call from inside the device's own allocator. |
| `mvfx_errorMissing` | A name or object the call needs is not in the table it was given, such as an asset of an effect file. |

`mvfxGetResultName` names any result for a log. `mvfxContext_GetMisuse` counts the calls refused as invalid since the context was made: a program can assert in its own tests that it stays at zero, and log it in a release build.

Each function's documentation in its header lists the results it returns and what each means there.

## An effect that is refused

`mvfxCreateEffect` checks every field of the def and its parts, with the ranges each header states; a refused def answers `mvfx_errorLimit` for a field past a named bound, `mvfx_errorInvalid` for any other (`mvfx_errorFormat` when it comes from a file, whatever the check). A def that is built up in steps is easiest to check one emitter at a time. An effect file refused by `mvfxReadEffect` comes with the offset of the first byte at fault. `tools/effect_json.py to-json` shows a file whole, and refuses a damaged one, a hash that does not match included.

## Particles that are missing

`mvfxInstance_GetEmitterStats` tells what happened to them:

- **`refused` above zero:** births past the emitter's capacity. Raise `capacity`, or lower the rate or the lifetime.
- **`killed`:** kill rules, killing colliders, an attractor's kill radius or a host module ended them.
- **`valuesKilled`:** a host module left them with a value that is not finite or is past its cap ([effects](effects.md#host-modules)).
- **`owed` but not `live`:** they were born and died within a step, which a very short lifetime does.
- **A lower quality level:** the context's particle budget lowered the instance's quality (`mvfxInstance_GetQuality`), which scales capacities and rates and may leave emitters out.
- **`status`:** an emitter that has stopped (its loops ended, or the instance was stopped) spawns no more.

The other counts (`eventsDropped`, `queriesDropped`, `neighborsCapped`, `contactsCapped`, `linksDropped` and the rest) each name the capacity that dropped work.

## Results that differ

The same effects, seeds, inputs and steps give the same bits everywhere. When two runs differ, one of these differs between them:

- **Seeds.** An instance with seed 0 takes its seed from the order the context created its instances.
- **Inputs between steps:** transforms, parameters, the program's spawns, colliders, scene answers, depth images, grids and meshes, and when they were set. How often an instance is moved changes where births along its way fall.
- **The steps:** their sequence in ticks. A span stepped in different pieces reaches the same state, but only when the inputs between the pieces are the same.
- **Host modules,** whose determinism is the program's to keep; `mvfxDrawModuleRandom` gives each module a stream that is deterministic.
- **The library's version.** A release may change results, and its changelog says so.

A thread's floating-point setting is not among them: a program may run its threads with flush-to-zero or another rounding mode, and every call that computes sets the library's own and gives the thread its setting back. The allocator and the task hooks run in the thread's setting; host modules run in the library's.

The GPU part matches the CPU path in its integer outputs and within stated tolerances in its floats, not to the bit; see [the GPU part](gpu.md#what-matches-the-cpu-path).

## A step that is slow

- **Capacities.** An emitter's cost follows its live particles, and its memory follows its capacity. Keep capacities near what an effect needs.
- **Task hooks.** A context without them steps every instance on the calling thread; with them, instances step in parallel, and gases and water surfaces spread their cells over tasks.
- **Unseen instances.** Throttle or pause them (`mvfxInstance_SetOffscreen`).
- **Quality levels and the budget** bound the particles a context holds.
- **Turbulence** baked into a grid with `mvfxBakeCurlNoise` costs a grid read a particle instead of three noise gradients.
- **Neighbors** cost a grid build a step and a query a particle; a smaller radius and `maxNeighbors` bound them.
- **Gases** cost their cell count times their substeps; a 2D gas or coarser cells cost far less.
