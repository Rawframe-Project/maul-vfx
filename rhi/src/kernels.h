// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The part's kernels: each one's container and entry, and the object's
// pipeline for it, which the part's passes and its tests set.

#ifndef MAUL_VFX_RHI_SRC_KERNELS_H
#define MAUL_VFX_RHI_SRC_KERNELS_H

#include "maul-rhi/pipeline.h"
#include "maul-vfx-rhi/gpu.h"

// The kernels, by index.
typedef enum mvfxKernel
{
    // pcg4d of each index, the words the CPU's must equal.
    mvfx_kernelPcg4d = 0,
    MVFX_KERNEL_COUNT
} mvfxKernel;

// A ready object's pipeline for a kernel.
mrhiComputePipelineId mvfxGpuPipeline(const mvfxGpu* gpu, mvfxKernel kernel);

#endif
