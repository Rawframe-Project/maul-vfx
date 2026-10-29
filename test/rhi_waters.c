// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rhi_waters.h"

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"
#include "water.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float s_cpu[WATER_CELLS];
static float s_gpu[WATER_CELLS];
static float s_worst;
static float s_moved;
static double s_poured;
static uint64_t s_held;

void PushWaters(const Run* run, int32_t s)
{
    if (s % 4 != 0 || s >= 24)
    {
        return;
    }
    double turn = 0.7 * (double)s;
    const mvfxWaterPush pushes[3] = {
        {.point = {0.9 * sin(turn), 0.0, -0.7 * cos(turn)}, .radius = 0.35f, .depth = 0.04f},
        {.point = {-1.2, 0.02, 0.6 + 0.05 * (double)s}, .radius = 0.6f, .depth = -0.03f},
        {.point = {2.3, 0.0, -0.1}, .radius = 0.5f, .depth = 0.05f}};
    int32_t count = s % 8 == 0 ? 3 : 2;
    for (int32_t w = 0; w < run->waterCount; ++w)
    {
        CHECK(mvfxInstance_PushWater(RunContext(), CpuInstance(), w, pushes, count) ==
                      mvfx_success &&
                  mvfxInstance_PushWater(RunContext(), PlannedInstance(), w, pushes, count) ==
                      mvfx_success,
              "the surfaces pushed");
    }
}

bool ReportWaters(Gpu* gpu, mvfxGpuSim* sim)
{
    mvfxGpuWater water = {0};
    bool reported = true;
    for (int32_t w = 0; reported && mvfxGpuSim_GetWater(sim, w, &water) == mvfx_success; ++w)
    {
        uint32_t record[MVFX_GPU_WATER_STATISTICS_WORDS];
        reported = ReadRange(gpu, water.statistics, 0, sizeof record, record, sizeof record) &&
                   mvfxGpuSim_ReportWater(sim, RunContext(), w, record) == mvfx_success;
    }
    return reported;
}

bool ReadWaterGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t water, const mvfxWaterRead* read,
                  uint64_t bytes, uint64_t offset, uint8_t fill, void* out)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = bytes;
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) != mrhi_success)
    {
        return false;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    mrhiPassId passes[2] = {{0}, {0}};
    mrhiRequestId request = {0};
    bool recorded = mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffer, &target) == mrhi_success;
    const mrhiAccess filled = {.resource = target, .kind = mrhi_accessCopyDestination};
    const mrhiAccess taken = {.resource = target, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &filled;
    def.accessCount = 1;
    def.neverCull = true;
    mvfxGpuWaterReadDef readDef = mvfxDefaultGpuWaterReadDef();
    readDef.water = water;
    readDef.read = *read;
    readDef.target = target;
    readDef.offset = offset;
    readDef.bytes = bytes - offset;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &passes[0]) == mrhi_success &&
               mvfxGpuSim_AddWaterRead(sim, &readDef) == mvfx_success;
    def.accesses = &taken;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &passes[1]) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mrhiPass_Begin(gpu->device, passes[0]) == mrhi_success;
    // The fill: words of fill bytes over the buffer.
    static uint8_t fills[1 << 16];
    memset(fills, fill, sizeof fills);
    for (uint64_t at = 0; recorded && at < bytes; at += sizeof fills)
    {
        uint64_t span = bytes - at < sizeof fills ? bytes - at : sizeof fills;
        recorded = mrhiPass_WriteBuffer(gpu->device, passes[0], target, at, fills, span) == mrhi_success;
    }
    recorded = recorded && mrhiPass_End(gpu->device, passes[0]) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiPass_Begin(gpu->device, passes[1]) == mrhi_success &&
               mrhiPass_ReadBuffer(gpu->device, passes[1], target, 0, bytes, &request) == mrhi_success &&
               mrhiPass_End(gpu->device, passes[1]) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
        (void)mrhiDestroyBuffer(gpu->device, buffer);
        return false;
    }
    size_t got = 0;
    bool done = Finish(gpu) &&
                mrhiRequest_TakeReadback(gpu->device, request, out, (size_t)bytes, &got) == mrhi_success &&
                got == bytes;
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    return done;
}

// A surface's field over its whole grid as float32 on both paths.
static bool SameField(Gpu* gpu, mvfxGpuSim* sim, int32_t w, const mvfxWaterDef* def,
                      mvfxWaterField field, float tolerance)
{
    int64_t cells = (int64_t)def->size[0] * def->size[1];
    const mvfxWaterRead read = {
        .field = field, .format = mvfx_formatFloat32, .size = {def->size[0], def->size[1]}};
    if (cells > WATER_CELLS ||
        mvfxInstance_ReadWater(RunContext(), CpuInstance(), w, &read, s_cpu, 4 * cells) !=
            mvfx_success ||
        !ReadWaterGpu(gpu, sim, w, &read, 4u * (uint64_t)cells, 0, 0, s_gpu))
    {
        printf("surface %d: field %d not read\n", w, field);
        return false;
    }
    bool same = true;
    for (int64_t i = 0; i < cells; ++i)
    {
        if (field >= mvfx_waterVelocityX && field <= mvfx_waterVelocityZ)
        {
            s_moved = fmaxf(s_moved, fabsf(s_cpu[i]));
        }
        float apart = fabsf(s_gpu[i] - s_cpu[i]) / (1.0f + fabsf(s_cpu[i]));
        s_worst = fmaxf(s_worst, apart);
        if (same && !(apart <= tolerance))
        {
            printf("surface %d: field %d, cell %lld: GPU %g, CPU %g\n", w, field, (long long)i,
                   (double)s_gpu[i], (double)s_cpu[i]);
            same = false;
        }
    }
    return same;
}

// A surface's counts and reach on both paths, as the planned instance's
// reports left them: the steps slowed and the faces held equal, the
// volume poured and the reach within the tolerance.
static bool SameCounts(int32_t w, float tolerance)
{
    mvfxWaterInfo cpu;
    mvfxWaterInfo planned;
    if (mvfxInstance_GetWaterInfo(RunContext(), CpuInstance(), w, &cpu) != mvfx_success ||
        mvfxInstance_GetWaterInfo(RunContext(), PlannedInstance(), w, &planned) != mvfx_success)
    {
        return false;
    }
    float cpuReach = mvfxContextInstance(RunContext(), CpuInstance())->waters[w].reach;
    float plannedReach = mvfxContextInstance(RunContext(), PlannedInstance())->waters[w].reach;
    double poured = fabs(planned.volumePoured - cpu.volumePoured) / (1.0 + cpu.volumePoured);
    float reach = fabsf(plannedReach - cpuReach) / (1.0f + cpuReach);
    s_poured = fmax(s_poured, cpu.volumePoured);
    s_held = cpu.facesHeld > s_held ? cpu.facesHeld : s_held;
    bool same = planned.stepsSlowed == cpu.stepsSlowed && planned.facesHeld == cpu.facesHeld &&
                poured <= (double)tolerance && reach <= tolerance;
    if (!same)
    {
        printf("surface %d: slowed %llu and %llu, held %llu and %llu, poured %g and %g, reach %g "
               "and %g (GPU and CPU)\n",
               w, (unsigned long long)planned.stepsSlowed, (unsigned long long)cpu.stepsSlowed,
               (unsigned long long)planned.facesHeld, (unsigned long long)cpu.facesHeld,
               planned.volumePoured, cpu.volumePoured, (double)plannedReach, (double)cpuReach);
    }
    return same;
}

bool SameWaters(Gpu* gpu, mvfxGpuSim* sim, const Run* run, float tolerance)
{
    bool same = true;
    for (int32_t w = 0; same && w < run->waterCount; ++w)
    {
        for (int32_t f = mvfx_waterHeight; same && f <= mvfx_waterDepth; ++f)
        {
            same = SameField(gpu, sim, w, &run->waters[w], (mvfxWaterField)f, tolerance);
        }
        same = same && SameCounts(w, tolerance);
    }
    return same;
}

float WaterError(void)
{
    return s_worst;
}

float WaterMoved(void)
{
    return s_moved;
}

double WaterPoured(void)
{
    return s_poured;
}

uint64_t WaterHeld(void)
{
    return s_held;
}

void ResetWaters(void)
{
    s_worst = 0.0f;
    s_moved = 0.0f;
    s_poured = 0.0;
    s_held = 0;
}
