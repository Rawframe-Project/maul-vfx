// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rhi_samples.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"

#include <math.h>
#include <string.h>

// The bytes before the points in their buffer and before the samples in
// theirs, so a query's offsets count.
#define POINT_OFFSET  20u
#define SAMPLE_OFFSET 36u

static bool s_withStep;

void SampleWithStep(bool on)
{
    s_withStep = on;
}

bool StepInFrame(mvfxGpuSim* sim)
{
    bool stepping = s_withStep;
    s_withStep = false;
    return !stepping || mvfxGpuSim_AddPasses(sim) == mvfx_success;
}

// What a query asks, and its words a point.
typedef struct Asked
{
    bool gas;
    int32_t index;
    mvfxGasField field;
    const double* origin;
    int64_t count;
    uint32_t words;
} Asked;

// Adds the query of the points' buffer and the samples' to the frame.
static bool AddQuery(mvfxGpuSim* sim, const Asked* asked, mrhiResourceId points,
                     mrhiResourceId target)
{
    uint64_t pointBytes = 12u * (uint64_t)asked->count;
    uint64_t bytes = 4u * asked->words * (uint64_t)asked->count;
    if (asked->gas)
    {
        mvfxGpuGasSamplesDef def = mvfxDefaultGpuGasSamplesDef();
        def.gas = asked->index;
        def.field = asked->field;
        memcpy(def.origin, asked->origin, sizeof def.origin);
        def.points = points;
        def.pointOffset = POINT_OFFSET;
        def.pointBytes = pointBytes;
        def.count = asked->count;
        def.target = target;
        def.offset = SAMPLE_OFFSET;
        def.bytes = bytes;
        return mvfxGpuSim_AddGasSamples(sim, &def) == mvfx_success;
    }
    mvfxGpuWaterSamplesDef def = mvfxDefaultGpuWaterSamplesDef();
    def.water = asked->index;
    memcpy(def.origin, asked->origin, sizeof def.origin);
    def.points = points;
    def.pointOffset = POINT_OFFSET;
    def.pointBytes = pointBytes;
    def.count = asked->count;
    def.target = target;
    def.offset = SAMPLE_OFFSET;
    def.bytes = bytes;
    return mvfxGpuSim_AddWaterSamples(sim, &def) == mvfx_success;
}

// Runs a query in a frame of its own (after the step's passes when
// asked): the points written past
// POINT_OFFSET, the query, the samples read back from past SAMPLE_OFFSET
// into out.
static bool RunQuery(Gpu* gpu, mvfxGpuSim* sim, const Asked* asked, const float* points, void* out)
{
    uint64_t pointBytes = 12u * (uint64_t)asked->count;
    uint64_t bytes = 4u * asked->words * (uint64_t)asked->count;
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffers[2] = {{0}, {0}};
    bufferDef.size = POINT_OFFSET + pointBytes;
    bool made = mrhiCreateBuffer(gpu->device, &bufferDef, &buffers[0]) == mrhi_success;
    bufferDef.size = SAMPLE_OFFSET + bytes;
    made = made && mrhiCreateBuffer(gpu->device, &bufferDef, &buffers[1]) == mrhi_success;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId ids[2] = {{0}, {0}};
    mrhiPassId passes[2] = {{0}, {0}};
    mrhiRequestId request = {0};
    bool recorded = made && mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffers[0], &ids[0]) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffers[1], &ids[1]) == mrhi_success;
    const mrhiAccess filled = {.resource = ids[0], .kind = mrhi_accessCopyDestination};
    const mrhiAccess taken = {.resource = ids[1], .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &filled;
    def.accessCount = 1;
    def.neverCull = true;
    recorded = recorded && StepInFrame(sim) &&
               mrhiAddPass(gpu->device, &def, &passes[0]) == mrhi_success &&
               AddQuery(sim, asked, ids[0], ids[1]);
    def.accesses = &taken;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &passes[1]) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mrhiPass_Begin(gpu->device, passes[0]) == mrhi_success &&
               mrhiPass_WriteBuffer(gpu->device, passes[0], ids[0], POINT_OFFSET, points,
                                    pointBytes) == mrhi_success &&
               mrhiPass_End(gpu->device, passes[0]) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiPass_Begin(gpu->device, passes[1]) == mrhi_success &&
               mrhiPass_ReadBuffer(gpu->device, passes[1], ids[1], SAMPLE_OFFSET, bytes,
                                   &request) == mrhi_success &&
               mrhiPass_End(gpu->device, passes[1]) == mrhi_success;
    size_t got = 0;
    bool done =
        recorded && Finish(gpu) &&
        mrhiRequest_TakeReadback(gpu->device, request, out, (size_t)bytes, &got) == mrhi_success &&
        got == bytes;
    if (made && !recorded)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
    }
    for (int32_t b = 0; b < 2; ++b)
    {
        if (buffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(gpu->device, buffers[b]);
        }
    }
    return done;
}

bool SampleWaterGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t water, const double origin[3],
                    const float* points, int64_t count, uint32_t* out)
{
    const Asked asked = {
        .index = water, .origin = origin, .count = count, .words = MVFX_GPU_WATER_SAMPLE_WORDS};
    return RunQuery(gpu, sim, &asked, points, out);
}

bool SampleGasGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t gas, mvfxGasField field,
                  const double origin[3], const float* points, int64_t count, float* out)
{
    const Asked asked = {
        .gas = true, .index = gas, .field = field, .origin = origin, .count = count, .words = 1};
    return RunQuery(gpu, sim, &asked, points, out);
}

int32_t LayGasPoints(const mvfxGasInfo* info, const double origin[3], float* points, double* world)
{
    int32_t inside = 0;
    double q[4];
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] = (double)info->rotation[k];
    }
    for (int32_t i = 0; i < GAS_SAMPLE_POINTS; ++i)
    {
        const int32_t at[3] = {i % 6, i / 6 % 6, i / 36};
        double v[3];
        bool in = true;
        for (int32_t k = 0; k < 3; ++k)
        {
            // A cell's edge in the box's frame (the info's is the world's).
            double cell = (double)info->cellSize / (double)info->scale;
            double side = cell * (info->size[k] > 1 ? info->size[k] : 20);
            v[k] = side * (1.5 * ((double)at[k] + 0.5) / 6.0 - 0.75) + 0.013 * (double)(k + 1);
            in = in && (info->size[k] == 1 || fabs(v[k]) < 0.5 * side);
        }
        inside += in ? 1 : 0;
        // Into the world: turned by the box, scaled, about its center.
        double t[3] = {2.0 * (q[1] * v[2] - q[2] * v[1]), 2.0 * (q[2] * v[0] - q[0] * v[2]),
                       2.0 * (q[0] * v[1] - q[1] * v[0])};
        const double turned[3] = {v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]),
                                  v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]),
                                  v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0])};
        // The CPU path from the binary32 points, as the device takes them.
        for (int32_t k = 0; k < 3; ++k)
        {
            double place = info->center[k] + (double)info->scale * turned[k];
            points[3 * i + k] = (float)(place - origin[k]);
            world[3 * i + k] = (double)points[3 * i + k] + origin[k];
        }
    }
    return inside;
}
