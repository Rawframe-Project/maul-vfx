// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The part's kernels: the containers that hold them, each kernel's entry,
// and the object's pipeline for it, which the part's passes and its tests
// set.

#ifndef MAUL_VFX_RHI_SRC_KERNELS_H
#define MAUL_VFX_RHI_SRC_KERNELS_H

#include "maul-rhi/pipeline.h"
#include "maul-vfx-rhi/gpu.h"

// The containers, a shader each.
typedef enum mvfxContainer
{
    mvfx_containerPcg4d = 0,
    mvfx_containerSim = 1,
    MVFX_CONTAINER_COUNT
} mvfxContainer;

// The kernels, a pipeline each.
typedef enum mvfxKernel
{
    // pcg4d of each index, the words the CPU's must equal.
    mvfx_kernelPcg4d = 0,
    // An emitter's step (sim.wgsl): its live particles moved, the
    // survivors kept in order, the plan's births spawned after them.
    mvfx_kernelUpdate = 1,
    mvfx_kernelCompact = 2,
    mvfx_kernelSpawn = 3,
    // Which of the plan's births have room, as the CPU path's full step
    // decides.
    mvfx_kernelRoom = 4,
    MVFX_KERNEL_COUNT
} mvfxKernel;

// The device an object runs on.
mrhiDevice* mvfxGpuDevice(const mvfxGpu* gpu);

// A ready object's pipeline for a kernel.
mrhiComputePipelineId mvfxGpuPipeline(const mvfxGpu* gpu, mvfxKernel kernel);

#endif
