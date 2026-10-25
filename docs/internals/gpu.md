# The GPU part

How `maul-vfx-rhi` ([rhi/](../../rhi)) runs a planned instance on a device, and how it stays the CPU path's equal. The user's view is [gpu.md](../user/gpu.md).

## The CPU path decides, the GPU path executes

A GPU particle system that decides its own births, deaths and randomness drifts from the CPU path, and two ports of every rule drift from each other. Here the core keeps every decision that needs the effect's logic: a planned instance ([src/planner.c](../../src/planner.c)) runs the emitter step's planning stages and writes plans ([include/maul-vfx/plan.h](../../include/maul-vfx/plan.h)) with the births owed, their serials, birth ticks and death ticks, the program's births, and the forces, colliders, kill rules, steering targets and liquid terms resolved for the step, in the CPU path's own arithmetic. Emitter views (`mvfxEffect_GetEmitterView`) expose the compiled effect: the attribute plan, baked tables, value sources. The GPU part then only executes: it never parses a def, and its kernels read the same tables the CPU path baked.

What only the device knows (whether a birth found room, how many particles are alive, whether a gas still holds anything) stays on the device. The plan says what is owed, and the kernels decide room by the same rules as the CPU path's step, in tick order where the emitter may fill.

## Kernels

The kernels are written once, in WGSL, under [rhi/kernels/](../../rhi/kernels). A line `// include: PATH.wgsl` splices a shared part in before compiling: `shared/` holds what several kernels use, a kernel's own directory the parts only it uses, split so no file passes the length limit. `tools/gen_kernels.py` compiles each kernel offline: naga to SPIR-V, `spirv-val` against Vulkan 1.3, then Maul RHI's tools cross it to DXIL (SPIRV-Cross and DXC) and to Metal (SPIRV-Cross), and pack every form with the WGSL into a Maul RHI shader container, committed as a C header (`rhi/kernels/*_container.h`). A build needs no shader tools, and every backend runs the same source.

The containers are large (the step kernel is most of them) and cannot be rebuilt in CI, which has none of those tools. CI runs `gen_kernels.py --check`, which needs none of them: each committed container must carry a fresh splice of its kernel's sources, byte for byte, so an edited kernel part without a rebuild fails. [kernels.md](../operations/kernels.md) describes regenerating them.

A GPU object ([rhi/src/kernels.c](../../rhi/src/kernels.c)) creates the shaders and requests the pipelines from the device, a bounded number at a time, built asynchronously as Maul RHI's requests are; nothing runs until all are ready.

## A step on the device

`mvfxGpuSim_AddPasses` ([rhi/src/sim.c](../../rhi/src/sim.c)) adds a transfer pass, which uploads the step's numbers and plans, and a fixed sequence of compute passes for each emitter: births placed by a prefix sum over the plan, deaths and kills compacted by a stable scan so the pool stays in birth order, the move, collision, kill rules, events and bounds. Neighbors, contacts, units, rigid bodies and liquids add passes of their own. Maul RHI's frame graph orders the passes and places every barrier from their declared accesses; the part declares, it never synchronizes by hand.

A birth's source age, which its values drawn over the emitter's age read, comes from the plan (`mvfxPlannedBirth.sourceAge`) for the emitter's own and the host's births, in the core's arithmetic. The kernels work one out only for a birth from events (`sourceAgeAt` in [rhi/kernels/sim/spawn.wgsl](../../rhi/kernels/sim/spawn.wgsl)). Such a birth is never before the emitter's start delay ends: the ticks whose events spawn start at the later of the delay and the plan's start (`mvfxEventSpawnWindow`, [src/spawn.c](../../src/spawn.c)), and `spawnsFrom` keeps only those. So `sourceAgeAt` never meets a negative count of ticks in a loop, and needs none of the core's clamp to the loop's start for a birth before the delay.

Nothing is read back within a step. Every count the step needs on the device (the live count, the room left) lives in buffers the next pass reads, and each emitter's statistics record (live count, bounds, the capacity counts) is left for the host to read back frames later. Bounds are measured as a box per block of the compacted pool, then reduced in one workgroup by a fixed tree, so the result does not depend on which thread finishes first.

Rigid bodies that meet each other solve their pairs colour by colour in one workgroup, as the CPU path solves them in order; that keeps them equal to the CPU path at the cost of using one compute unit.

## Equal within tolerances

The device's floating-point arithmetic differs from the CPU's (fused operations, other rounding of `sqrt` and transcendental functions, other evaluation orders), so floats cannot be bit-equal. The part promises instead:

- **Integers are equal.** Serials, birth and death ticks, live counts and every count the plan and the step produce match the CPU path, because they come from the plan's integer arithmetic or from comparisons both paths make the same way.
- **Floats are within stated tolerances**, relative to each value's scale, which the headers state per output and the parity tests measure.
- **Reproducible on one device and driver:** the same inputs give the same bits run after run, since no kernel depends on the order threads finish.

The parity tests (`test/test_rhi_*.c` through [test/rhi_harness.c](../../test/rhi_harness.c)) step scenes on both paths side by side and compare every particle after every step, on lavapipe under the Vulkan validation layer.

## What is refused, and why

`mvfxCreateGpuSim` refuses at creation what it does not run, rather than failing at a frame:

- Host modules and scene queries run the host's code and read the host's world, which only the CPU has.
- Contacts beside anything that kills within a step: the CPU path's contacts still see particles killed in the same step, which the device's compaction has already removed. Contacts beside ribbons and beams likewise: the CPU path takes ribbon points before contacts move the births.
- Rigid particles beside anything that kills within a step, for the same reason as contacts.

Each refusal names the first emitter, gas or water surface at fault and the feature bits why (`mvfxGpuRefusal`), so a tool can tell an author what to change.

## Memory and budgets

A simulation takes all of its device memory when it is made ([rhi/src/allocator.c](../../rhi/src/allocator.c)): each emitter's buffers at its capacity, its sort and event scratch, and room for the instance's grids and meshes set aside at sizes its def gives ([rhi/src/grids.c](../../rhi/src/grids.c)). Grids are uploaded only when their serial changes. The passes and upload bytes each step takes are bounded and stated (`mvfxGpuSim_EmitterPasses`, `mvfxGpuSim_UploadBytes`), so a host can size Maul RHI's frame limits once.
