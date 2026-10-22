// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "kernels.h"

#include "allocator.h"

#include "../kernels/contacts_container.h"
#include "../kernels/gas_container.h"
#include "../kernels/gas_read_container.h"
#include "../kernels/grid_container.h"
#include "../kernels/liquid_container.h"
#include "../kernels/liquid_gas_container.h"
#include "../kernels/pcg4d_container.h"
#include "../kernels/plexus_container.h"
#include "../kernels/records_container.h"
#include "../kernels/ribbon_records_container.h"
#include "../kernels/self_container.h"
#include "../kernels/sim_container.h"
#include "../kernels/sort_container.h"
#include "../kernels/splat_container.h"
#include "../kernels/water_container.h"
#include "../kernels/water_read_container.h"
#include "maul-rhi/shader.h"

#include <stdalign.h>
#include <string.h>

// The pipeline requests an object holds unanswered at once: each of its
// answers requests the next kernel's, so a device answering into a queue
// of its own room (Maul RHI's mrhi_errorCapacity) or a driver holding a
// few creations at a time is never asked for every kernel at once.
#define PENDING_REQUESTS 32
static_assert(PENDING_REQUESTS <= MVFX_KERNEL_COUNT, "a window within the kernels");

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
    {s_contactsContainer, sizeof s_contactsContainer},
    {s_plexusContainer, sizeof s_plexusContainer},
    {s_selfContainer, sizeof s_selfContainer},
    {s_gasContainer, sizeof s_gasContainer},
    {s_gasReadContainer, sizeof s_gasReadContainer},
    {s_splatContainer, sizeof s_splatContainer},
    {s_liquidContainer, sizeof s_liquidContainer},
    {s_liquidGasContainer, sizeof s_liquidGasContainer},
    {s_waterContainer, sizeof s_waterContainer},
    {s_waterReadContainer, sizeof s_waterReadContainer},
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
    {mvfx_containerContacts, "contactRadii"},
    {mvfx_containerContacts, "contactKeys"},
    {mvfx_containerContacts, "contactProject"},
    {mvfx_containerContacts, "contactCommit"},
    {mvfx_containerContacts, "contactImpulses"},
    {mvfx_containerContacts, "contactKick"},
    {mvfx_containerSim, "rebox"},
    {mvfx_containerPlexus, "plexusKeys"},
    {mvfx_containerPlexus, "plexusCount"},
    {mvfx_containerPlexus, "plexusOffsets"},
    {mvfx_containerPlexus, "plexusWrite"},
    {mvfx_containerSim, "units"},
    {mvfx_containerRibbonRecords, "sheetFit"},
    {mvfx_containerRibbonRecords, "sheetPoints"},
    {mvfx_containerSelf, "selfReach"},
    {mvfx_containerSelf, "selfKeys"},
    {mvfx_containerSelf, "selfGather"},
    {mvfx_containerSelf, "selfPart"},
    {mvfx_containerSelf, "selfCommit"},
    {mvfx_containerSim, "rigid"},
    {mvfx_containerSim, "rigidPairs"},
    {mvfx_containerGas, "gasAreas"},
    {mvfx_containerGas, "gasCoefficients"},
    {mvfx_containerGas, "gasPourCells"},
    {mvfx_containerGas, "gasFaces"},
    {mvfx_containerGas, "gasCurl"},
    {mvfx_containerGas, "gasConfine"},
    {mvfx_containerGas, "gasCarry"},
    {mvfx_containerGas, "gasCarryCells"},
    {mvfx_containerGas, "gasBurn"},
    {mvfx_containerGas, "gasMean"},
    {mvfx_containerGas, "gasDivergence"},
    {mvfx_containerGas, "gasJacobi"},
    {mvfx_containerGas, "gasRestrict"},
    {mvfx_containerGas, "gasProlong"},
    {mvfx_containerGas, "gasTail"},
    {mvfx_containerGas, "gasCorrect"},
    {mvfx_containerGas, "gasHolds"},
    {mvfx_containerGas, "gasMark"},
    {mvfx_containerGas, "gasEmpty"},
    {mvfx_containerGas, "gasMove"},
    {mvfx_containerGasRead, "gasRead"},
    {mvfx_containerSplat, "splatKeys"},
    {mvfx_containerSplat, "splatRuns"},
    {mvfx_containerSplat, "splatCells"},
    {mvfx_containerSplat, "splatFaces"},
    {mvfx_containerSim, "liquidMove"},
    {mvfx_containerSim, "liquidSlide"},
    {mvfx_containerLiquid, "liquidKeys"},
    {mvfx_containerLiquid, "liquidMultipliers"},
    {mvfx_containerLiquid, "liquidCorrections"},
    {mvfx_containerLiquid, "liquidGain"},
    {mvfx_containerLiquid, "liquidViscosity"},
    {mvfx_containerLiquid, "liquidCurls"},
    {mvfx_containerLiquid, "liquidConfine"},
    {mvfx_containerLiquid, "liquidKeep"},
    {mvfx_containerLiquidGas, "liquidGasKeys"},
    {mvfx_containerLiquidGas, "liquidGasRuns"},
    {mvfx_containerLiquidGas, "liquidGasMark"},
    {mvfx_containerLiquidGas, "liquidGasCoarse"},
    {mvfx_containerLiquidGas, "liquidGasGather"},
    {mvfx_containerLiquidGas, "liquidGasKeep"},
    {mvfx_containerLiquidGas, "liquidGasExtend"},
    {mvfx_containerLiquidGas, "liquidGasWalls"},
    {mvfx_containerLiquidGas, "liquidGasRead"},
    {mvfx_containerWater, "waterSink"},
    {mvfx_containerWater, "waterSubstep"},
    {mvfx_containerWater, "waterSettle"},
    {mvfx_containerWaterRead, "waterRead"},
    {mvfx_containerWater, "shallowStart"},
    {mvfx_containerWater, "shallowSpill"},
    {mvfx_containerWater, "shallowFaces"},
    {mvfx_containerWater, "shallowGiving"},
    {mvfx_containerWater, "shallowDepths"},
    {mvfx_containerWater, "shallowSettle"},
};

struct mvfxGpu
{
    mvfxAllocator allocator;
    mrhiDevice* device;
    mrhiShaderId shaders[MVFX_CONTAINER_COUNT];
    mrhiComputePipelineId pipelines[MVFX_KERNEL_COUNT];
    mrhiRequestId requests[MVFX_KERNEL_COUNT];
    bool built[MVFX_KERNEL_COUNT];
    bool answered[MVFX_KERNEL_COUNT];
    // The next kernel to request, the requests unanswered, and whether
    // the device refused one after creation (never ready then).
    int32_t next;
    int32_t pending;
    bool refused;
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
    for (; made && gpu->next < PENDING_REQUESTS; ++gpu->next)
    {
        made = Request(gpu, gpu->next);
    }
    gpu->pending = gpu->next;
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
            if (!gpu->answered[k])
            {
                gpu->answered[k] = true;
                --gpu->pending;
            }
            // The next requests, while the window has room.
            while (!gpu->refused && gpu->pending < PENDING_REQUESTS &&
                   gpu->next < MVFX_KERNEL_COUNT)
            {
                gpu->refused = !Request(gpu, gpu->next);
                gpu->pending += gpu->refused ? 0 : 1;
                gpu->next += gpu->refused ? 0 : 1;
            }
            return true;
        }
    }
    return false;
}

bool mvfxGpu_IsReady(const mvfxGpu* gpu)
{
    bool ready = gpu != nullptr && !gpu->refused;
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
