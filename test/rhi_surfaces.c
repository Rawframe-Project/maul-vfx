// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rhi_surfaces.h"

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"
#include "maul-vfx/output.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

static float s_cpu[SURFACE_CELLS];
static float s_gpu[SURFACE_CELLS];
static float s_worst;
static float s_least = FLT_MAX;

// Writes gas g's distance over its whole box as float32 through
// mvfxGpuSim_AddGasRead, and reads it back into out.
static bool ReadGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t g, const mvfxGasRead* read, uint64_t bytes,
                    void* out)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = bytes;
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource;
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) != mrhi_success)
    {
        return false;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    mrhiPassId pass = {0};
    mrhiRequestId request = {0};
    bool recorded = mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffer, &target) == mrhi_success;
    const mrhiAccess taken = {.resource = target, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &taken;
    def.accessCount = 1;
    def.neverCull = true;
    mvfxGpuGasReadDef readDef = mvfxDefaultGpuGasReadDef();
    readDef.gas = g;
    readDef.read = *read;
    readDef.target = target;
    readDef.bytes = bytes;
    recorded = recorded && mvfxGpuSim_AddGasRead(sim, &readDef) == mvfx_success &&
               mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiPass_Begin(gpu->device, pass) == mrhi_success &&
               mrhiPass_ReadBuffer(gpu->device, pass, target, 0, bytes, &request) == mrhi_success &&
               mrhiPass_End(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
        (void)mrhiDestroyBuffer(gpu->device, buffer);
        return false;
    }
    size_t got = 0;
    bool done =
        Finish(gpu) &&
        mrhiRequest_TakeReadback(gpu->device, request, out, (size_t)bytes, &got) == mrhi_success &&
        got == bytes;
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    return done;
}

// Surface gas g's distance on both paths.
static bool SameDistance(Gpu* gpu, mvfxGpuSim* sim, int32_t g, const mvfxGasDef* def,
                         float tolerance)
{
    int64_t cells = (int64_t)def->size[0] * def->size[1] * def->size[2];
    const mvfxGasRead read = {.field = mvfx_gasDistance,
                              .format = mvfx_formatFloat32,
                              .size = {def->size[0], def->size[1], def->size[2]}};
    if (cells > SURFACE_CELLS ||
        mvfxInstance_ReadGas(RunContext(), CpuInstance(), g, mvfx_gasDistance, s_cpu, cells) !=
            mvfx_success ||
        !ReadGpu(gpu, sim, g, &read, 4u * (uint64_t)cells, s_gpu))
    {
        printf("gas %d: its distance not read\n", g);
        return false;
    }
    bool same = true;
    for (int64_t i = 0; i < cells; ++i)
    {
        s_least = fminf(s_least, s_cpu[i]);
        float apart = fabsf(s_gpu[i] - s_cpu[i]) / (1.0f + fabsf(s_cpu[i]));
        s_worst = fmaxf(s_worst, apart);
        if (same && !(apart <= tolerance))
        {
            printf("gas %d: cell %lld: GPU %g, CPU %g\n", g, (long long)i, (double)s_gpu[i],
                   (double)s_cpu[i]);
            same = false;
        }
    }
    return same;
}

bool SameSurfaces(Gpu* gpu, mvfxGpuSim* sim, const Run* run, float tolerance)
{
    bool same = true;
    for (int32_t g = 0; same && g < run->gasCount; ++g)
    {
        same = run->gases[g].kind != mvfx_gasSurface ||
               SameDistance(gpu, sim, g, &run->gases[g], tolerance);
    }
    return same;
}

float SurfaceError(void)
{
    return s_worst;
}

float SurfaceLeast(void)
{
    return s_least;
}

void ResetSurfaces(void)
{
    s_worst = 0.0f;
    s_least = FLT_MAX;
}
