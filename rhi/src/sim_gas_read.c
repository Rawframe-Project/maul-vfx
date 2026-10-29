// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas read out on the GPU, as the core's src/gas_read.c reads it: a
// field over a box of cells in a host's format (gas_read.wgsl), and a
// field at a host's points (gas_sample.wgsl), the points in the world
// less an origin and the box's map rebased on it, so the kernel works in
// binary32 near them. Each read or query is a transfer pass writing its
// numbers and a compute pass, recorded after the step.

#include "samples.h"
#include "sim_gas.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

// Makes a uniform buffer of a read's or a query's numbers.
static bool CreateNumbers(mvfxGpuSim* sim, uint64_t words, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 4u * words;
    def.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    return mvfxSimMade(sim, mrhiCreateBuffer(sim->device, &def, out));
}

bool mvfxCreateSimGasReads(mvfxGpuSim* sim, SimGas* gas)
{
    bool created = true;
    for (int32_t r = 0; created && r < MVFX_GPU_GAS_READS; ++r)
    {
        created = CreateNumbers(sim, GAS_READ_WORDS, &gas->reads[r].numbers);
    }
    for (int32_t q = 0; created && q < MVFX_GPU_GAS_SAMPLES; ++q)
    {
        created = CreateNumbers(sim, GAS_SAMPLE_WORDS, &gas->samples[q].numbers);
    }
    return created;
}

static void DestroyNumbers(mvfxGpuSim* sim, mrhiBufferId numbers)
{
    if (numbers.index1 != 0)
    {
        (void)mrhiDestroyBuffer(sim->device, numbers);
    }
}

void mvfxDestroySimGasReads(mvfxGpuSim* sim, SimGas* gas)
{
    for (int32_t r = 0; r < MVFX_GPU_GAS_READS; ++r)
    {
        DestroyNumbers(sim, gas->reads[r].numbers);
    }
    for (int32_t q = 0; q < MVFX_GPU_GAS_SAMPLES; ++q)
    {
        DestroyNumbers(sim, gas->samples[q].numbers);
    }
}

// Adds a read's or a query's passes: a transfer pass writing its
// numbers, then a compute pass of its accesses; false when the device
// refuses one.
static bool AddPasses(mvfxGpuSim* sim, mrhiResourceId numbers, const mrhiAccess* used,
                      uint32_t count, mrhiPassId passes[2])
{
    const mrhiAccess written = {.resource = numbers, .kind = mrhi_accessCopyDestination};
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = &written;
    pass.accessCount = 1;
    pass.neverCull = true;
    bool added = mrhiAddPass(sim->device, &pass, &passes[0]) == mrhi_success;
    pass.passClass = mrhi_passGraphics;
    pass.accesses = used;
    pass.accessCount = count;
    return added && mrhiAddPass(sim->device, &pass, &passes[1]) == mrhi_success;
}

// Dispatches a pass over points, in rows past GAS_ROW workgroups.
static bool Dispatch(mvfxGpuSim* sim, mrhiPassId pass, uint32_t points)
{
    uint32_t groups = (points + GAS_GROUP - 1u) / GAS_GROUP;
    return groups == 0 || mrhiPass_Dispatch(sim->device, pass, groups < GAS_ROW ? groups : GAS_ROW,
                                       (groups + GAS_ROW - 1u) / GAS_ROW, 1) == mrhi_success;
}

// Records a read's passes: its numbers' write, then its words.
static bool RecordRead(mvfxGpuSim* sim, const SimGas* gas, const GasRead* read)
{
    bool recorded = mrhiPass_Begin(sim->device, read->passes[0]) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, read->passes[0], read->resource, 0,
                               read->words, sizeof read->words);
    recorded = mrhiPass_End(sim->device, read->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[4] = {
        {.slot = 0, .resource = gas->resources[GAS_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = gas->resources[GAS_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = read->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = read->target, .size = MRHI_WHOLE_SIZE}};
    mrhiPassId pass = read->passes[1];
    bool ran =
        recorded && mrhiPass_Begin(sim->device, pass) == mrhi_success &&
        mrhiPass_SetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, mvfx_kernelGasRead)) ==
            mrhi_success &&
        mrhiPass_SetBindings(sim->device, pass, 0, bindings, 4) == mrhi_success &&
        Dispatch(sim, pass, read->written);
    return mrhiPass_End(sim->device, pass) == mrhi_success && ran;
}

// Records a sample query's passes: its numbers' write, then its samples.
static bool RecordQuery(mvfxGpuSim* sim, const SimGas* gas, const GasSamples* query)
{
    bool recorded = mrhiPass_Begin(sim->device, query->passes[0]) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, query->passes[0], query->resource, 0,
                               query->words, sizeof query->words);
    recorded = mrhiPass_End(sim->device, query->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[5] = {
        {.slot = 0, .resource = gas->resources[GAS_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = gas->resources[GAS_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = query->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = query->target, .size = MRHI_WHOLE_SIZE},
        {.slot = 4, .resource = query->points, .size = MRHI_WHOLE_SIZE}};
    mrhiPassId pass = query->passes[1];
    bool ran =
        recorded && mrhiPass_Begin(sim->device, pass) == mrhi_success &&
        mrhiPass_SetComputePipeline(sim->device, pass,
                               mvfxGpuPipeline(sim->gpu, mvfx_kernelGasSample)) == mrhi_success &&
        mrhiPass_SetBindings(sim->device, pass, 0, bindings, 5) == mrhi_success &&
        Dispatch(sim, pass, query->count);
    return mrhiPass_End(sim->device, pass) == mrhi_success && ran;
}

bool mvfxRecordSimGasReads(mvfxGpuSim* sim, SimGas* gas)
{
    bool recorded = true;
    for (int32_t r = 0; recorded && r < gas->readCount; ++r)
    {
        recorded = RecordRead(sim, gas, &gas->reads[r]);
    }
    for (int32_t q = 0; recorded && q < gas->sampleCount; ++q)
    {
        recorded = RecordQuery(sim, gas, &gas->samples[q]);
    }
    return recorded;
}

mvfxGpuGasReadDef mvfxDefaultGpuGasReadDef(void)
{
    return (mvfxGpuGasReadDef){
        .cookie = MVFX_GPU_GAS_READ_DEF_COOKIE,
        .read = {.field = mvfx_gasDensity, .format = mvfx_formatFloat32, .size = {1, 1, 1}}};
}

// A format's bytes a value, 0 for none.
static uint32_t FormatBytes(mvfxFormat format)
{
    switch (format)
    {
    case mvfx_formatFloat32:
        return 4;
    case mvfx_formatFloat16:
    case mvfx_formatUnorm16:
    case mvfx_formatSnorm16:
        return 2;
    case mvfx_formatUnorm8:
    case mvfx_formatSnorm8:
        return 1;
    default:
        return 0;
    }
}

// A read's numbers, false for one out of range for the gas or its buffer
// (as the core's IsReadValid, and its bytes within 32-bit addresses).
static bool ReadWords(const SimGas* gas, const mvfxGpuGasReadDef* def, uint32_t words[20],
                      uint32_t* written)
{
    const mvfxGasRead* read = &def->read;
    const int32_t* n = gas->view.def.size;
    uint32_t bytes = FormatBytes(read->format);
    bool normalized = read->format != mvfx_formatFloat32 && read->format != mvfx_formatFloat16;
    // A surface's one field, its distance, is its first scalar.
    bool surface = gas->view.def.kind == mvfx_gasSurface;
    bool field = surface ? read->field == mvfx_gasDistance
                         : read->field >= mvfx_gasDensity && read->field <= mvfx_gasVelocityZ;
    bool valid =
        field && bytes > 0 &&
        (!normalized || (isfinite(read->low) && isfinite(read->high) && read->high > read->low));
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = read->first[k] >= 0 && read->size[k] >= 1 && read->size[k] <= n[k] - read->first[k];
    }
    if (!valid)
    {
        return false;
    }
    uint64_t row = (uint64_t)read->size[0] * bytes;
    uint64_t rowPitch = read->rowBytes == 0 ? row : read->rowBytes;
    uint64_t layer = rowPitch * (uint64_t)read->size[1];
    uint64_t layerPitch = read->layerBytes == 0 ? layer : read->layerBytes;
    uint64_t end =
        layerPitch * (uint64_t)(read->size[2] - 1) + rowPitch * (uint64_t)(read->size[1] - 1) + row;
    if (rowPitch < row || layerPitch < layer || def->offset % 4u != 0 || end > def->bytes ||
        def->offset + end > UINT32_MAX)
    {
        return false;
    }
    *written = (uint32_t)((def->offset + end + 3u) / 4u - def->offset / 4u);
    const uint32_t head[20] = {surface ? (uint32_t)mvfx_gasDensity : (uint32_t)read->field,
                               (uint32_t)read->format,
                               gas->added ? gas->after : gas->now,
                               *written,
                               (uint32_t)read->first[0],
                               (uint32_t)read->first[1],
                               (uint32_t)read->first[2],
                               0,
                               (uint32_t)read->size[0],
                               (uint32_t)read->size[1],
                               (uint32_t)read->size[2],
                               bytes,
                               (uint32_t)rowPitch,
                               (uint32_t)layerPitch,
                               (uint32_t)def->offset,
                               (uint32_t)end};
    memcpy(words, head, sizeof head);
    mvfxFloatWords(words + 16, read->low, read->high, 0.0f, 0.0f);
    return true;
}

mvfxResult mvfxGpuSim_AddGasRead(mvfxGpuSim* sim, const mvfxGpuGasReadDef* def)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_GAS_READ_DEF_COOKIE ||
        def->gas < 0 || def->gas >= sim->gasCount || def->target.index1 == 0)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    SimGas* gas = &sim->gases[def->gas];
    if (gas->readCount == MVFX_GPU_GAS_READS)
    {
        return mvfx_errorLimit;
    }
    GasRead* read = &gas->reads[gas->readCount];
    if (!ReadWords(gas, def, read->words, &read->written))
    {
        return mvfx_errorInvalid;
    }
    read->target = def->target;
    bool imported = mrhiImportBuffer(sim->device, read->numbers, &read->resource) == mrhi_success &&
                    mvfxImportSimGas(sim, gas);
    const mrhiResourceId* r = gas->resources;
    const mrhiAccess used[4] = {{.resource = r[GAS_NUMBERS], .kind = mrhi_accessUniform},
                                {.resource = r[GAS_FIELDS], .kind = mrhi_accessStorageRead},
                                {.resource = read->resource, .kind = mrhi_accessUniform},
                                {.resource = def->target, .kind = mrhi_accessStorageReadWrite}};
    if (!imported || !AddPasses(sim, read->resource, used, 4, read->passes))
    {
        return mvfx_errorState;
    }
    gas->readCount += 1;
    return mvfx_success;
}

mvfxGpuGasSamplesDef mvfxDefaultGpuGasSamplesDef(void)
{
    return (mvfxGpuGasSamplesDef){.cookie = MVFX_GPU_GAS_SAMPLES_DEF_COOKIE,
                                  .field = mvfx_gasDensity};
}

// A query's numbers (gas_sample.wgsl's Sample): its count, the field (a
// surface's distance its first scalar), the buffer current, the points'
// first word; the box's map into its frame; the samples' first word, and
// the value outside the box (a surface's reach, as the core's
// mvfxSurfaceReach, else 0).
static void SampleWords(const SimGas* gas, const mvfxGpuGasSamplesDef* def, const mvfxGasInfo* info,
                        uint32_t words[GAS_SAMPLE_WORDS])
{
    const mvfxGasDef* d = &gas->view.def;
    bool surface = d->kind == mvfx_gasSurface;
    const uint32_t head[4] = {
        (uint32_t)def->count, surface ? (uint32_t)mvfx_gasDensity : (uint32_t)def->field,
        gas->added ? gas->after : gas->now, (uint32_t)(def->pointOffset / 4u)};
    memcpy(words, head, sizeof head);
    mvfxSampleMapWords(def->origin, info->center, info->rotation, info->scale, words + 4);
    float outside = surface ? d->surface.reach * d->surface.radius : 0.0f;
    uint32_t bits;
    memcpy(&bits, &outside, sizeof bits);
    const uint32_t tail[4] = {(uint32_t)(def->offset / 4u), bits, 0, 0};
    memcpy(words + 12, tail, sizeof tail);
}

mvfxResult mvfxGpuSim_AddGasSamples(mvfxGpuSim* sim, const mvfxGpuGasSamplesDef* def)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_GAS_SAMPLES_DEF_COOKIE ||
        def->gas < 0 || def->gas >= sim->gasCount)
    {
        return mvfx_errorInvalid;
    }
    SimGas* gas = &sim->gases[def->gas];
    bool surface = gas->view.def.kind == mvfx_gasSurface;
    bool field = surface ? def->field == mvfx_gasDistance
                         : def->field >= mvfx_gasDensity && def->field <= mvfx_gasVelocityZ;
    const SampleQuery asked = {.origin = def->origin,
                               .points = def->points,
                               .pointOffset = def->pointOffset,
                               .pointBytes = def->pointBytes,
                               .count = def->count,
                               .target = def->target,
                               .offset = def->offset,
                               .bytes = def->bytes};
    if (!field || !mvfxIsSampleQueryValid(&asked, 1u))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    if (gas->sampleCount == MVFX_GPU_GAS_SAMPLES)
    {
        return mvfx_errorLimit;
    }
    mvfxGasInfo info;
    if (mvfxInstance_GetGasInfo(sim->context, sim->instance, def->gas, &info) != mvfx_success)
    {
        return mvfx_errorState;
    }
    GasSamples* samples = &gas->samples[gas->sampleCount];
    SampleWords(gas, def, &info, samples->words);
    samples->count = (uint32_t)def->count;
    samples->points = def->points;
    samples->target = def->target;
    bool imported =
        mrhiImportBuffer(sim->device, samples->numbers, &samples->resource) == mrhi_success &&
        mvfxImportSimGas(sim, gas);
    const mrhiResourceId* r = gas->resources;
    const mrhiAccess used[5] = {{.resource = r[GAS_NUMBERS], .kind = mrhi_accessUniform},
                                {.resource = r[GAS_FIELDS], .kind = mrhi_accessStorageRead},
                                {.resource = samples->resource, .kind = mrhi_accessUniform},
                                {.resource = def->target, .kind = mrhi_accessStorageReadWrite},
                                {.resource = def->points, .kind = mrhi_accessStorageRead}};
    if (!imported || !AddPasses(sim, samples->resource, used, 5, samples->passes))
    {
        return mvfx_errorState;
    }
    gas->sampleCount += 1;
    return mvfx_success;
}
