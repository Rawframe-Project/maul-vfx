# Memory

Every byte the library uses comes from the host's allocator, at a moment the host chooses: when it makes an object. Stepping, writing output and reading results allocate nothing, and nothing grows by itself. A game that budgets memory per level, runs on a console with fixed pools, or simply wants no allocation in its frame loop gets that from the API's shape, not from tuning.

## Who owns what

The **context** is the owner object. Its def takes the allocator (`alloc` and `free` with a size and an alignment, a context pointer, no realloc; zeroed means the C library's) and named limits: how many effects, instances, meshes, grids and layouts may exist at once. The context allocates its tables of those objects once, at those limits, and refuses a creation past one with `mvfx_errorCapacity`.

Objects are named by ids: a 1-based slot in the context's table and the generation the slot had when the object took it ([src/slots.h](../../src/slots.h)). A destroyed object's slot bumps its generation, so an old id is told apart from the slot's next occupant and refused with `mvfx_errorStale`; nothing is ever reached through a dangling pointer.

## One block per object

- **An effect** compiles into one block ([src/effect.h](../../src/effect.h)): its header, its emitters with their checked values, the tables its curves and gradients bake into, its gases' and water surfaces' defs, and each emitter's attribute plan. The def's arrays are not referenced after creation.
- **An instance** allocates everything it will hold in one block ([src/instance.h](../../src/instance.h)): each emitter's particle arrays at its capacity (at the instance's quality level), its event buffer, its scene queries, its neighbor grid, its units' and rigid bodies' working storage, its gases' and water surfaces' fields, and the scratch its step needs. Capacities are fixed at creation; a birth past one is dropped and counted, never stored elsewhere.
- **Grids, meshes and layouts** are a block each, sized from their defs.

Block layouts are computed with checked arithmetic ([src/allocator.h](../../src/allocator.h)): a size that would overflow is refused with `mvfx_errorCapacity` before anything is allocated. For alignments beyond `max_align_t`, the C library's functions are asked for a larger block and the aligned part of it is used.

## Particle storage

An emitter's particles are structure of arrays: one array per attribute, only the attributes its def uses (its attribute plan), each 64-byte aligned and padded to a multiple of 16 particles. An emitter that never reads colour stores none, and a loop over one attribute touches only that attribute's cache lines and runs in whole vector widths without a scalar tail. The same arrays, in the same order, are what host modules see (`mvfxParticleArrays`) and what the GPU part mirrors on the device.

Values over life are not stored: they are computed where read from the initial values and the age ([simulation.md](simulation.md#values-a-particle-carries)).

## Memory the host gives per call

Output and reads write into the host's buffers and take scratch from the host: a sort by place, a sprite expansion and a surface mesh each state the scratch they need (`mvfxInstance_GetSortScratchBytes`, `mvfxInstance_GetSurfaceMeshScratchBytes`). A buffer too small takes the first records of the order and the call still reports the full count, so a host can size its buffers once and learn when they were short. Reading an effect file puts the def's arrays into memory the host gives, after reporting how much it needs.

## The particle budget

A context may cap the particles its instances hold (`mvfxContextLimits.particles`): the sum of their emitters' capacities at their quality levels. An instance takes the first quality level from the one it asks for whose capacities fit beside what the context's instances hold; when none does, its creation is refused with `mvfx_errorCapacity`. The budget counts capacities, not live particles, because capacities are what memory is allocated for.

## The allocator calling back in

The allocator must not call the context's functions that change it, since one may be allocating at the time. The context counts its calls of host code (a step's tasks and host modules, every allocation), and refuses a changing call made meanwhile with `mvfx_errorBusy`, counted as misuse. Functions that only read (those taking a `const` context) are allowed.

## The GPU part

A simulation takes its host-side memory from its own def's allocator, and its device memory from the device when it is made: each emitter's buffers at its capacity, room for the instance's grids and meshes set aside at a size the def gives, and the passes' scratch. It refuses a grid or mesh too large for its room when it is set, so a running effect never meets an allocation failure mid-frame ([gpu.md](gpu.md)).
