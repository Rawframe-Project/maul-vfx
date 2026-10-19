// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "kernels.h"

#include "allocator.h"

#include "../kernels/grid_container.h"
#include "../kernels/pcg4d_container.h"
#include "../kernels/records_container.h"
#include "../kernels/ribbon_records_container.h"
#include "../kernels/sim_container.h"
#include "../kernels/sort_container.h"
#include "maul-rhi/shader.h"

#include <stdalign.h>
#include <string.h>

// A container's bytes.
typedef struct Container
{
    const uint8_t* bytes;
    size_t size;
} Container;

static const Container s_containers[MVFX_CONTAINER_COUNT] = {
    {s_pcg4dContainer, sizeof s_pcg4dContainer},
    {s_simContainer, sizeof s_simContainer},
    {s_recordsContainer, sizeof s_recordsContainer},
    {s_sortContainer, sizeof s_sortContainer},
    {s_ribbonRecordsContainer, sizeof s_ribbonRecordsContainer},
    {s_gridContainer, sizeof s_gridContainer},
};

// A kernel's container and entry.
typedef struct Kernel
{
    mvfxContainer container;
    const char* entry;
} Kernel;

static const Kernel s_kernels[MVFX_KERNEL_COUNT] = {
    {mvfx_containerPcg4d, "pcg"},
    {mvfx_containerSim, "update"},
    {mvfx_containerSim, "room"},
    {mvfx_containerSim, "reduce"},
    {mvfx_containerSim, "offsets"},
    {mvfx_containerSim, "scatter"},
    {mvfx_containerSim, "measure"},
    {mvfx_containerSim, "killBirths"},
    {mvfx_containerRecords, "records"},
    {mvfx_containerRecords, "keys"},
    {mvfx_containerSort, "offsets"},
    {mvfx_containerSort, "scatter"},
    {mvfx_containerSim, "eventCount"},
    {mvfx_containerSim, "eventOffsets"},
    {mvfx_containerSim, "eventWrite"},
    {mvfx_containerRecords, "eventValues"},
    {mvfx_containerSim, "spawnCount"},
    {mvfx_containerSim, "spawnSort"},
    {mvfx_containerSim, "spawnScan"},
    {mvfx_containerSim, "spawnMerge"},
    {mvfx_containerSim, "ribbons"},
    {mvfx_containerRibbonRecords, "ribbonKeys"},
    {mvfx_containerRibbonRecords, "ribbonChains"},
    {mvfx_containerRibbonRecords, "ribbonPoints"},
    {mvfx_containerRibbonRecords, "ribbonPieces"},
    {mvfx_containerSim, "beam"},
    {mvfx_containerRecords, "lightCount"},
    {mvfx_containerRecords, "lightOffsets"},
    {mvfx_containerRecords, "lightWrite"},
    {mvfx_containerRibbonRecords, "spriteQuads"},
    {mvfx_containerGrid, "gridKeys"},
    {mvfx_containerGrid, "gridValues"},
    {mvfx_containerGrid, "neighbors"},
};

struct mvfxGpu
{
    mvfxAllocator allocator;
    mrhiDevice* device;
    mrhiShaderId shaders[MVFX_CONTAINER_COUNT];
    mrhiComputePipelineId pipelines[MVFX_KERNEL_COUNT];
    mrhiRequestId requests[MVFX_KERNEL_COUNT];
    bool built[MVFX_KERNEL_COUNT];
};

mvfxGpuDef mvfxDefaultGpuDef(void)
{
    return (mvfxGpuDef){.cookie = MVFX_GPU_DEF_COOKIE};
}

// Creates container c's shader.
static bool CreateShader(mvfxGpu* gpu, int32_t c)
{
    mrhiShaderDef shader = mrhiDefaultShaderDef();
    shader.bytes = s_containers[c].bytes;
    shader.byteCount = s_containers[c].size;
    return mrhiCreateShader(gpu->device, &shader, &gpu->shaders[c]) == mrhi_success;
}

// Requests kernel k's pipeline from its container's shader.
static bool Request(mvfxGpu* gpu, int32_t k)
{
    mrhiComputePipelineDef pipeline = mrhiDefaultComputePipelineDef();
    pipeline.shader = gpu->shaders[s_kernels[k].container];
    pipeline.entry = s_kernels[k].entry;
    pipeline.entryLength = strlen(s_kernels[k].entry);
    return mrhiCreateComputePipeline(gpu->device, &pipeline, &gpu->pipelines[k],
                                     &gpu->requests[k]) == mrhi_success;
}

mvfxResult mvfxCreateGpu(const mvfxGpuDef* def, mvfxGpu** gpuOut)
{
    if (gpuOut != nullptr)
    {
        *gpuOut = nullptr;
    }
    if (def == nullptr || gpuOut == nullptr || def->cookie != MVFX_GPU_DEF_COOKIE ||
        def->device == nullptr || !mvfxRhiIsAllocatorValid(&def->allocator))
    {
        return mvfx_errorInvalid;
    }
    mvfxGpu* gpu = mvfxRhiAllocate(&def->allocator, sizeof *gpu, alignof(mvfxGpu));
    if (gpu == nullptr)
    {
        return mvfx_errorCapacity;
    }
    *gpu = (mvfxGpu){0};
    gpu->allocator = def->allocator;
    gpu->device = def->device;
    bool made = true;
    for (int32_t c = 0; made && c < MVFX_CONTAINER_COUNT; ++c)
    {
        made = CreateShader(gpu, c);
    }
    for (int32_t k = 0; made && k < MVFX_KERNEL_COUNT; ++k)
    {
        made = Request(gpu, k);
    }
    if (!made)
    {
        mvfxDestroyGpu(gpu);
        return mvfx_errorState;
    }
    *gpuOut = gpu;
    return mvfx_success;
}

void mvfxDestroyGpu(mvfxGpu* gpu)
{
    if (gpu == nullptr)
    {
        return;
    }
    // Each was created, so its destruction is not refused; a creation that
    // failed left its id null.
    for (int32_t k = 0; k < MVFX_KERNEL_COUNT; ++k)
    {
        if (gpu->pipelines[k].index1 != 0)
        {
            (void)mrhiDestroyComputePipeline(gpu->device, gpu->pipelines[k]);
        }
    }
    for (int32_t c = 0; c < MVFX_CONTAINER_COUNT; ++c)
    {
        if (gpu->shaders[c].index1 != 0)
        {
            (void)mrhiDestroyShader(gpu->device, gpu->shaders[c]);
        }
    }
    mvfxAllocator allocator = gpu->allocator;
    mvfxRhiRelease(&allocator, gpu, sizeof *gpu, alignof(mvfxGpu));
}

bool mvfxGpu_Notify(mvfxGpu* gpu, const mrhiDeviceNotification* notification)
{
    if (gpu == nullptr || notification == nullptr || notification->kind != mrhi_devicePipelineReady)
    {
        return false;
    }
    for (int32_t k = 0; k < MVFX_KERNEL_COUNT; ++k)
    {
        if (notification->request.index1 == gpu->requests[k].index1 &&
            notification->request.generation == gpu->requests[k].generation)
        {
            gpu->built[k] = notification->outcome == mrhi_success;
            return true;
        }
    }
    return false;
}

bool mvfxGpu_IsReady(const mvfxGpu* gpu)
{
    bool ready = gpu != nullptr;
    for (int32_t k = 0; ready && k < MVFX_KERNEL_COUNT; ++k)
    {
        ready = gpu->built[k];
    }
    return ready;
}

mrhiComputePipelineId mvfxGpuPipeline(const mvfxGpu* gpu, mvfxKernel kernel)
{
    return gpu->pipelines[kernel];
}

mrhiDevice* mvfxGpuDevice(const mvfxGpu* gpu)
{
    return gpu->device;
}
