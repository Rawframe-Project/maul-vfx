// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface sampled at a host's points on the GPU
// (water_sample.wgsl), as the core's mvfxInstance_SampleWater samples it:
// the host's points in the world less an origin, the surface's map into
// its frame rebased on that origin in binary64 and rounded once, so the
// kernel works in binary32 near the points; a query a transfer pass
// writing its numbers and a compute pass, recorded after the step and the
// reads.

#include "kernels.h"
#include "numbers.h"
#include "sim_state.h"
#include "sim_water.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

// A query's workgroups a row of its dispatch (a device takes 65,535 along
// a dimension; water_sample.wgsl's ROW invocations), and a workgroup's
// invocations.
#define SAMPLE_ROW   32768u
#define SAMPLE_GROUP 64u

bool mvfxCreateSimWaterSamples(mvfxGpuSim* sim, SimWater* water)
{
    bool created = true;
    for (int32_t q = 0; created && q < MVFX_GPU_WATER_SAMPLES; ++q)
    {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = 4u * WATER_SAMPLE_WORDS;
        def.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
        created = mrhiCreateBuffer(sim->device, &def, &water->samples[q].numbers) == mrhi_success;
    }
    return created;
}

void mvfxDestroySimWaterSamples(mvfxGpuSim* sim, SimWater* water)
{
    for (int32_t q = 0; q < MVFX_GPU_WATER_SAMPLES; ++q)
    {
        if (water->samples[q].numbers.index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, water->samples[q].numbers);
        }
    }
}

mvfxGpuWaterSamplesDef mvfxDefaultGpuWaterSamplesDef(void)
{
    return (mvfxGpuWaterSamplesDef){.cookie = MVFX_GPU_WATER_SAMPLES_DEF_COOKIE};
}

// Turns a binary64 vector by a unit quaternion, as the core's
// mvfxRotateDouble.
static void RotateDouble(const float q[4], const double v[3], double out[3])
{
    double x = (double)q[0];
    double y = (double)q[1];
    double z = (double)q[2];
    double w = (double)q[3];
    double tx = 2.0 * (y * v[2] - z * v[1]);
    double ty = 2.0 * (z * v[0] - x * v[2]);
    double tz = 2.0 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

// Whether a query is in range: its origin, its count, its buffers and
// their bytes.
static bool IsQueryValid(const mvfxGpuWaterSamplesDef* def)
{
    bool valid = def->count >= 0 && def->count <= MVFX_GPU_SAMPLE_POINTS &&
                 def->points.index1 != 0 && def->target.index1 != 0 &&
                 def->points.index1 != def->target.index1 && def->pointOffset % 4u == 0 &&
                 def->offset % 4u == 0 && def->pointOffset <= UINT32_MAX - 4u &&
                 def->offset <= UINT32_MAX - 4u;
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = fabs(def->origin[k]) <= 0x1p52;
    }
    uint64_t count = valid ? (uint64_t)def->count : 0u;
    return valid && def->pointBytes >= 12u * count &&
           def->bytes >= 4u * count * MVFX_GPU_WATER_SAMPLE_WORDS;
}

// A query's numbers (water_sample.wgsl's Sample): its count, the buffer
// current, its first words; the surface's map into its frame from the
// world less the origin (the core's mvfxBoxMap of the world: the box's
// turn back, 1 / scale, its center), rebased on the origin.
static void SampleWords(const SimWater* water, const mvfxGpuWaterSamplesDef* def,
                        const mvfxWaterInfo* info, uint32_t words[WATER_SAMPLE_WORDS])
{
    const uint32_t head[4] = {(uint32_t)def->count, water->added ? water->after : water->now,
                              (uint32_t)(def->pointOffset / 4u), (uint32_t)(def->offset / 4u)};
    memcpy(words, head, sizeof head);
    const float turn[4] = {-info->rotation[0], -info->rotation[1], -info->rotation[2],
                           info->rotation[3]};
    float sigma = 1.0f / info->scale;
    double from[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        from[k] = (def->origin[k] - info->center[k]) * (double)sigma;
    }
    double into[3];
    RotateDouble(turn, from, into);
    memcpy(words + 4, turn, sizeof turn);
    mvfxFloatWords(words + 8, (float)into[0], (float)into[1], (float)into[2], sigma);
    mvfxFloatWords(words + 12, (float)(info->center[0] - def->origin[0]),
                   (float)(info->center[1] - def->origin[1]),
                   (float)(info->center[2] - def->origin[2]), 0.0f);
}

mvfxResult mvfxGpuSim_AddWaterSamples(mvfxGpuSim* sim, const mvfxGpuWaterSamplesDef* def)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_WATER_SAMPLES_DEF_COOKIE ||
        def->water < 0 || def->water >= sim->waterCount || !IsQueryValid(def))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    SimWater* water = &sim->waters[def->water];
    if (water->sampleCount == MVFX_GPU_WATER_SAMPLES)
    {
        return mvfx_errorLimit;
    }
    mvfxWaterInfo info;
    if (mvfxInstance_GetWaterInfo(sim->context, sim->instance, def->water, &info) != mvfx_success)
    {
        return mvfx_errorState;
    }
    WaterSamples* query = &water->samples[water->sampleCount];
    SampleWords(water, def, &info, query->words);
    query->count = (uint32_t)def->count;
    query->points = def->points;
    query->target = def->target;
    bool imported =
        mrhiImportBuffer(sim->device, query->numbers, &query->resource) == mrhi_success &&
        mvfxImportSimWater(sim, water);
    const mrhiResourceId* r = water->resources;
    const mrhiAccess written = {.resource = query->resource, .kind = mrhi_accessCopyDestination};
    const mrhiAccess used[5] = {{.resource = r[WATER_NUMBERS], .kind = mrhi_accessUniform},
                                {.resource = r[WATER_FIELDS], .kind = mrhi_accessStorageRead},
                                {.resource = query->resource, .kind = mrhi_accessUniform},
                                {.resource = def->target, .kind = mrhi_accessStorageReadWrite},
                                {.resource = def->points, .kind = mrhi_accessStorageRead}};
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = &written;
    pass.accessCount = 1;
    pass.neverCull = true;
    bool added = imported && mrhiAddPass(sim->device, &pass, &query->passes[0]) == mrhi_success;
    pass.passClass = mrhi_passGraphics;
    pass.accesses = used;
    pass.accessCount = 5;
    added = added && mrhiAddPass(sim->device, &pass, &query->passes[1]) == mrhi_success;
    if (!added)
    {
        return mvfx_errorState;
    }
    water->sampleCount += 1;
    return mvfx_success;
}

// Records a query's passes: its numbers' write, then its samples.
static bool RecordQuery(mvfxGpuSim* sim, const SimWater* water, const WaterSamples* query)
{
    bool recorded = mrhiBeginPass(sim->device, query->passes[0]) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, query->passes[0], query->resource, 0,
                               query->words, sizeof query->words);
    recorded = mrhiEndPass(sim->device, query->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[5] = {
        {.slot = 0, .resource = water->resources[WATER_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = water->resources[WATER_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = query->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = query->target, .size = MRHI_WHOLE_SIZE},
        {.slot = 4, .resource = query->points, .size = MRHI_WHOLE_SIZE}};
    mrhiPassId pass = query->passes[1];
    uint32_t groups = (query->count + SAMPLE_GROUP - 1u) / SAMPLE_GROUP;
    bool ran =
        recorded && mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mrhiSetComputePipeline(sim->device, pass,
                               mvfxGpuPipeline(sim->gpu, mvfx_kernelWaterSample)) == mrhi_success &&
        mrhiSetBindings(sim->device, pass, 0, bindings, 5) == mrhi_success &&
        (groups == 0 || mrhiDispatch(sim->device, pass, groups < SAMPLE_ROW ? groups : SAMPLE_ROW,
                                     (groups + SAMPLE_ROW - 1u) / SAMPLE_ROW, 1) == mrhi_success);
    return mrhiEndPass(sim->device, pass) == mrhi_success && ran;
}

bool mvfxRecordSimWaterSamples(mvfxGpuSim* sim, SimWater* water)
{
    bool recorded = true;
    for (int32_t q = 0; recorded && q < water->sampleCount; ++q)
    {
        recorded = RecordQuery(sim, water, &water->samples[q]);
    }
    return recorded;
}
