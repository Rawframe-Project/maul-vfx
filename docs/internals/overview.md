# Overview

How Maul VFX is put together, what its domain adds to the [family's rules](maul-conventions.md), and where each part is explained.

## The shape

```
host ── public API (include/maul-vfx/)
           │
        the context: allocator, task hooks, tables of effects, instances,
           │         layouts, grids and meshes (src/context.c, slots.c)
           │
        effects: defs checked and compiled into one block each
           │      (effect_check.c, effect.c, value.c, curve.c, shape.c, ...),
           │      each refusal naming its field (field_path.c)
           │      ── effect files read and written (file.c, file_codec.c)
           │
        instances: one block each; the step advances them
           │      (step.c, emitter_step.c, spawn.c, birth.c, integrate.c,
           │       neighbor.c, unit.c, rigid*.c, liquid.c, gas*.c, water*.c)
           │
        output: records, quads, ribbons, lights, reads, meshes, written
                 into the host's buffers (output.c, sort.c, expand.c, ribbon.c, ...)

maul-vfx-rhi (rhi/): a planned instance's plans executed on a Maul RHI
                     device, in WGSL kernels (rhi/src/, rhi/kernels/)
```

The **core** is plain C with no dependency beyond the C library. Everything it decides is deterministic ([determinism.md](determinism.md)), and everything it holds comes from the host's allocator when an object is made ([memory.md](memory.md)).

**The GPU part** is a separate library beside the core, which the core never includes. It executes plans the core writes and decides nothing the core decides ([gpu.md](gpu.md)).

## Where each part is explained

| Topic | Page |
|---|---|
| Same bits everywhere: ticks, pcg4d, floating point, threads | [determinism.md](determinism.md) |
| The context's step, an emitter's step, the integrator | [simulation.md](simulation.md) |
| Owner objects, blocks, particle storage, the budget | [memory.md](memory.md) |
| The effect file's layout, codec and checks | [effect-file.md](effect-file.md) |
| Records, conversions, sorting, ribbons, bounds | [output.md](output.md) |
| Neighbors, contacts, units and rigid bodies | [interaction.md](interaction.md) |
| Gases, liquids, surfaces and water | [fluids.md](fluids.md) |
| Colliders, scene queries and the depth image | [collision.md](collision.md) |
| The GPU part | [gpu.md](gpu.md) |
| Words used throughout | [glossary.md](glossary.md) |

## The library's profile

The family asks each library to state what its domain adds to the common rules.

- **Determinism.** Bit-exact on the CPU path: the same effects, seeds, host inputs and sequence of step lengths give the same bits on every platform, compiler and architecture the family builds for, whatever the host's threads, for every particle attribute, count, event, bound, ribbon point and output record. The GPU part is reproducible on one device and driver, equals the CPU path in every integer output, and is compared with it within measured tolerances for floats.
- **Threads.** The library starts none. A context's parallel work runs on the host's task system through two hooks in its def; without them, serially on the calling thread. A context is used by one thread at a time; outputs for different views of a stepped context may be written from different threads at once.
- **Calling back in.** The allocator, the task hooks and host modules call none of the context's functions that take it mutable; only those taking a `const` context are allowed. Such a call is refused with `mvfx_errorBusy` and counted as misuse: the context counts its calls of host code (a step or a prewarm, whose tasks and host modules are the host's, and each allocation). The GPU part's allocators call none of its functions either; a simulation refuses its changing calls the same way, while its destruction releases its memory.
- **Stale ids.** Not misuse. A host keeps an instance's id while effects play and finish, and asks about it after it is destroyed in the normal course (`mvfxInstance_IsValid` exists for that), so a stale instance, mesh, grid or layout id counts nothing and the call returns `mvfx_errorStale`. An index out of range within a live instance, such as an emitter or a gas, is misuse.
- **Untrusted data.** Not misuse. An effect file is refused with its status and offset and counts nothing, including when what it asks of the program's assets does not fit them (`mvfxCreateEffectFromBytes`); the same def passed by the program to `mvfxCreateEffect` counts one. The bytes' pointer and size and the asset table are the program's own arguments, so an invalid one counts one too. A mesh whose vertices span no volume has no hull (`mvfxMesh_GetHullCenter` says so, `mvfxMesh_GetHullVertexCount` counts 0), and a rigid hull over it is refused with `mvfx_errorUnsupported`, never counted: its vertices come from a model file.
- **Memory.** The owner object is the context, which takes the host's allocator. An effect allocates its compiled form in one block, an instance all of its storage in one block at capacities fixed then. Stepping and output allocate nothing, nothing grows by itself, and every particle, point or event a capacity refuses is counted.
- **Platform dependencies.** The C standard library's `<stddef.h>`, `<stdint.h>`, `<string.h>`, `<float.h>` and the exactly rounded functions of `<math.h>`; the floating-point control register intrinsics of x86-64 and AArch64. No operating system call: no clock, file or thread. The GPU part depends on Maul RHI.
- **Data formats.** Of the family's three kinds of data a library writes, the effect file ([effect-file.md](effect-file.md)) is an asset format: tagged fields after its header, each field that holds its default left out, an unknown tag refused with `mvfx_errorVersion`. There are no state formats: the library saves no running context, instance or simulation. The GPU part's kernel containers are data the library generates from its own pinned WGSL sources, compiled into the GPU part: they belong to the release that built them. Records, plans and the GPU part's buffers are read within one process and release, and are no format.
- **Plan layouts.** A host's own GPU path reads the plans and views of `plan.h`, so from 1.0 their layouts hold as the family's rules hold any public struct's: a minor release adds a field only at the end of its struct, a patch release changes none, and the `MVFX_MAX_*` limits that size their arrays change only in a minor release. They sit in arrays whose stride is their size, so a program runs only against the minor release it was built with, which the shared library's SOVERSION (major.minor from 1.0, `cmake/Library.cmake`) enforces. `mvfxPlannedBirth` is held further: a GPU buffer holds the births as they are, so its 48 bytes keep their layout through every 1.x release (`src/planner.c` asserts its size and every field's offset), and a later need of a birth's becomes an array of its own beside them.
- **Size.** CI reports the static library's code size on Linux and its size on the web; neither is gated.

## Source layout

Files are named by their part: `context.c`, `effect.c`, `instance.c` and their `*_api.c` companions hold the objects and their public functions; `step.c` and `emitter_step.c` the step; `gas_*`, `water_*`, `rigid_*` and `liquid*` their solvers; `file*` the effect file; `output.c`, `expand.c`, `ribbon.c`, `tube.c` and `sort.c` the output. `src/marching_table.c`, `src/file_fields.c`, `src/file_tags.h`, `test/file_pins.h` and `test/file_layout.h` are generated by `tools/` and say so in their first lines. The module graph in `tools/modules.txt` says which file may include which, and keeps the dependencies one-way. The GPU part is `rhi/src` and `rhi/include` (listed in `tools/source-dirs.txt`), with its kernels' WGSL sources and compiled containers in `rhi/kernels`.
