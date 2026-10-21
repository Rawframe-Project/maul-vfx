// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's step on the GPU (water.wgsl): its numbers from its
// view and each step's plan, a pass of its own a dispatch (the frame
// graph orders passes, not a pass's dispatches); the host's reads of its
// fields (water_read.wgsl).

#include "sim_water.h"

#include "numbers.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"

#include <math.h>
#include <string.h>

// The first words of the numbers' parts (water_numbers.wgsl's Water).
#define WORD_SIZE    0u
#define WORD_FACTORS 4u
#define WORD_HEAD    8u
#define WORD_SINK    12u
#define WORD_PUSHES  16u

// The workgroups of a cell pass.
#define WATER_GROUP 64u

// A read's workgroups a row of its dispatch (a device takes 65,535 along
// a dimension; water_read.wgsl's ROW invocations).
#define READ_ROW 32768u

// Whether the part runs a surface: waves.
static bool Runs(const mvfxWaterView* view)
{
    return view->def.model == mvfx_waterWaves;
}

mvfxResult mvfxReadSimWaters(mvfxGpuSim* sim, mvfxEffectId effect, int32_t* refusal)
{
    for (int32_t w = 0; w < MVFX_MAX_WATERS; ++w)
    {
        SimWater* water = &sim->waters[w];
        mvfxResult result = mvfxEffect_GetWaterView(sim->context, effect, w, &water->view);
        if (result == mvfx_errorInvalid)
        {
            break;
        }
        if (result != mvfx_success)
        {
            return result;
        }
        sim->waterCount = w + 1;
        if (!Runs(&water->view))
        {
            *refusal = w;
            return mvfx_errorState;
        }
    }
    return mvfx_success;
}

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

bool mvfxCreateSimWater(mvfxGpuSim* sim, SimWater* water)
{
    const mvfxWaterDef* def = &water->view.def;
    uint32_t* w = water->words;
    w[WORD_SIZE] = (uint32_t)def->size[0];
    w[WORD_SIZE + 1] = (uint32_t)def->size[1];
    w[WORD_SIZE + 2] = (uint32_t)water->view.cells;
    w[WORD_SIZE + 3] = def->closedWalls;
    mvfxFloatWords(w + WORD_SINK, def->cellSize, 0.0f, def->center[1], 0.0f);
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiBufferUsage numbers = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    bool created =
        CreateBuffer(sim, 4u * MVFX_WATER_WORDS, numbers, &water->buffers[WATER_NUMBERS]) &&
        CreateBuffer(sim, 16u * (uint64_t)water->view.cells, written | mrhi_bufferCopySource,
                     &water->buffers[WATER_FIELDS]) &&
        CreateBuffer(sim, 4u * MVFX_GPU_WATER_STATISTICS_WORDS, written | mrhi_bufferCopySource,
                     &water->buffers[WATER_STATISTICS]);
    for (int32_t r = 0; created && r < MVFX_GPU_WATER_READS; ++r)
    {
        created = CreateBuffer(sim, 4u * WATER_READ_WORDS, numbers, &water->reads[r].numbers);
    }
    return created;
}

void mvfxDestroySimWater(mvfxGpuSim* sim, SimWater* water)
{
    for (int32_t b = 0; b < WATER_BUFFERS; ++b)
    {
        if (water->buffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, water->buffers[b]);
        }
    }
    for (int32_t r = 0; r < MVFX_GPU_WATER_READS; ++r)
    {
        if (water->reads[r].numbers.index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, water->reads[r].numbers);
        }
    }
}

// Appends a dispatch to the step.
static void Push(SimWater* water, mvfxKernel kernel, uint32_t now, uint32_t groups)
{
    water->dispatches[water->count++] =
        (WaterDispatch){.kernel = kernel, .now = now, .groups = groups};
}

// The step's numbers and dispatches, as the core's mvfxStepWater takes the
// plan: the pushes' sinks and the even lift, the substeps, the settle.
static void Plan(SimWater* water, const mvfxWaterPlan* plan)
{
    const mvfxWaterDef* def = &water->view.def;
    uint32_t* w = water->words;
    water->clears = plan->cleared;
    water->count = 0;
    // A substep's factors, as the core's Waves.
    float h = def->cellSize;
    float c = def->waveSpeed;
    float dt = plan->dt;
    mvfxFloatWords(w + WORD_FACTORS, dt * c * c / (h * h),
                   (float)mvfxExpMinus((double)(def->damping * dt)), (c * dt - h) / (c * dt + h),
                   dt);
    w[WORD_HEAD] = (uint32_t)plan->pushCount;
    float lift = (float)(plan->pushSank / (double)water->view.cells);
    mvfxFloatWords(w + WORD_SINK, h, lift, def->center[1], 0.0f);
    for (int32_t p = 0; p < plan->pushCount; ++p)
    {
        const mvfxPlannedWaterPush* push = &plan->pushes[p];
        mvfxFloatWords(w + WORD_PUSHES + 4u * (uint32_t)p, push->at[0], push->at[1], push->radius,
                       push->depth);
    }
    uint32_t groups = ((uint32_t)water->view.cells + WATER_GROUP - 1u) / WATER_GROUP;
    uint32_t now = plan->cleared ? 0 : water->now;
    if (plan->pushCount > 0)
    {
        Push(water, mvfx_kernelWaterSink, now, groups);
    }
    for (int32_t s = 0; s < plan->substeps; ++s)
    {
        Push(water, mvfx_kernelWaterSubstep, now, groups);
        now = 1u - now;
    }
    Push(water, mvfx_kernelWaterSettle, now, 1);
    uint64_t end = (uint64_t)(plan->start + plan->ticks);
    water->record[MVFX_GPU_WATER_HOLDS] = 0;
    water->record[MVFX_GPU_WATER_TICK] = (uint32_t)end;
    water->record[MVFX_GPU_WATER_TICK + 1] = (uint32_t)(end >> 32u);
    water->record[MVFX_GPU_WATER_SPEED] = 0;
    water->record[MVFX_GPU_WATER_REACH] = 0;
    water->after = now;
}

// Imports a surface's buffers into the frame open, once a frame.
static bool Import(mvfxGpuSim* sim, SimWater* water)
{
    for (int32_t b = 0; !water->imported && b < WATER_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, water->buffers[b], &water->resources[b]) != mrhi_success)
        {
            return false;
        }
    }
    water->imported = true;
    return true;
}

mvfxResult mvfxAddSimWaterPasses(mvfxGpuSim* sim, int32_t w)
{
    SimWater* water = &sim->waters[w];
    // The reads of a frame the host dropped go.
    water->added = false;
    water->readCount = 0;
    water->imported = false;
    mvfxWaterPlan plan;
    mvfxResult result = mvfxSimWaterPlan(sim, w, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    Plan(water, &plan);
    if (!Import(sim, water))
    {
        return mvfx_errorState;
    }
    const mrhiResourceId* r = water->resources;
    const mrhiAccess written[WATER_BUFFERS] = {
        {.resource = r[WATER_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = r[WATER_FIELDS], .kind = mrhi_accessCopyDestination},
        {.resource = r[WATER_STATISTICS], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[WATER_BUFFERS] = {
        {.resource = r[WATER_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[WATER_FIELDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[WATER_STATISTICS], .kind = mrhi_accessStorageReadWrite}};
    for (int32_t p = -1; p < water->count; ++p)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p < 0 ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p < 0 ? written : used;
        def.accessCount = WATER_BUFFERS;
        // Its writes outlive the frame, read by the next step or the host.
        def.neverCull = true;
        if (mrhiAddPass(sim->device, &def, &water->passes[p + 1]) != mrhi_success)
        {
            return mvfx_errorState;
        }
    }
    water->added = true;
    return mvfx_success;
}

// Records the pass writing the numbers and the record, and the fields
// emptied after a clear.
static bool RecordWrite(mvfxGpuSim* sim, const SimWater* water)
{
    mrhiPassId pass = water->passes[0];
    const mrhiResourceId* r = water->resources;
    bool recorded = mrhiBeginPass(sim->device, pass) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, pass, r[WATER_NUMBERS], 0, water->words,
                               sizeof water->words) &&
                    mvfxUpload(sim->device, &sim->uploaded, pass, r[WATER_STATISTICS], 0,
                               water->record, sizeof water->record);
    if (recorded && water->clears)
    {
        recorded =
            mrhiClearBuffer(sim->device, pass, r[WATER_FIELDS], 0, MRHI_WHOLE_SIZE) == mrhi_success;
    }
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records the step's dispatches.
static bool RecordStep(mvfxGpuSim* sim, const SimWater* water)
{
    bool recorded = RecordWrite(sim, water);
    mrhiBinding bindings[WATER_BUFFERS];
    for (int32_t b = 0; b < WATER_BUFFERS; ++b)
    {
        bindings[b] = (mrhiBinding){
            .slot = (uint32_t)b, .resource = water->resources[b], .size = MRHI_WHOLE_SIZE};
    }
    for (int32_t p = 0; recorded && p < water->count; ++p)
    {
        const WaterDispatch* d = &water->dispatches[p];
        mrhiPassId pass = water->passes[p + 1];
        const uint32_t root[4] = {d->now, 0, 0, 0};
        bool ran = mrhiBeginPass(sim->device, pass) == mrhi_success &&
                   mrhiSetComputePipeline(sim->device, pass,
                                          mvfxGpuPipeline(sim->gpu, d->kernel)) == mrhi_success &&
                   mrhiSetBindings(sim->device, pass, 0, bindings, WATER_BUFFERS) == mrhi_success &&
                   mrhiSetRootBlock(sim->device, pass, 0, root, sizeof root) == mrhi_success &&
                   mrhiDispatch(sim->device, pass, d->groups, 1, 1) == mrhi_success;
        recorded = mrhiEndPass(sim->device, pass) == mrhi_success && ran;
    }
    return recorded;
}

// Records a read's passes: its numbers' write, then its words.
static bool RecordRead(mvfxGpuSim* sim, const SimWater* water, const WaterRead* read)
{
    bool recorded = mrhiBeginPass(sim->device, read->passes[0]) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, read->passes[0], read->resource, 0,
                               read->words, sizeof read->words);
    recorded = mrhiEndPass(sim->device, read->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[4] = {
        {.slot = 0, .resource = water->resources[WATER_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = water->resources[WATER_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = read->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = read->target, .size = MRHI_WHOLE_SIZE}};
    mrhiPassId pass = read->passes[1];
    uint32_t groups = (read->written + WATER_GROUP - 1u) / WATER_GROUP;
    bool ran =
        recorded && mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mrhiSetComputePipeline(sim->device, pass,
                               mvfxGpuPipeline(sim->gpu, mvfx_kernelWaterRead)) == mrhi_success &&
        mrhiSetBindings(sim->device, pass, 0, bindings, 4) == mrhi_success &&
        (groups == 0 || mrhiDispatch(sim->device, pass, groups < READ_ROW ? groups : READ_ROW,
                                     (groups + READ_ROW - 1u) / READ_ROW, 1) == mrhi_success);
    return mrhiEndPass(sim->device, pass) == mrhi_success && ran;
}

bool mvfxRecordSimWater(mvfxGpuSim* sim, SimWater* water)
{
    bool recorded = !water->added || RecordStep(sim, water);
    water->now = water->added ? water->after : water->now;
    water->added = false;
    for (int32_t r = 0; recorded && r < water->readCount; ++r)
    {
        recorded = RecordRead(sim, water, &water->reads[r]);
    }
    water->readCount = 0;
    water->imported = false;
    return recorded;
}

mvfxGpuWaterReadDef mvfxDefaultGpuWaterReadDef(void)
{
    return (mvfxGpuWaterReadDef){
        .cookie = MVFX_GPU_WATER_READ_DEF_COOKIE,
        .read = {.field = mvfx_waterHeight, .format = mvfx_formatFloat32, .size = {1, 1}}};
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

// A read's numbers, false for one out of range for the surface or its
// buffer (as the core's IsReadValid, and its bytes within 32-bit
// addresses).
static bool ReadWords(const SimWater* water, const mvfxGpuWaterReadDef* def,
                      uint32_t words[WATER_READ_WORDS], uint32_t* written)
{
    const mvfxWaterRead* read = &def->read;
    const int32_t* n = water->view.def.size;
    uint32_t bytes = FormatBytes(read->format);
    bool normalized = read->format != mvfx_formatFloat32 && read->format != mvfx_formatFloat16;
    bool valid =
        read->field <= mvfx_waterDepth && bytes > 0 &&
        (!normalized || (isfinite(read->low) && isfinite(read->high) && read->high > read->low));
    for (int32_t k = 0; valid && k < 2; ++k)
    {
        valid = read->first[k] >= 0 && read->size[k] >= 1 && read->size[k] <= n[k] - read->first[k];
    }
    if (!valid)
    {
        return false;
    }
    uint64_t row = (uint64_t)read->size[0] * bytes;
    uint64_t pitch = read->rowBytes == 0 ? row : read->rowBytes;
    uint64_t end = pitch * (uint64_t)(read->size[1] - 1) + row;
    if (pitch < row || def->offset % 4u != 0 || end > def->bytes || def->offset + end > UINT32_MAX)
    {
        return false;
    }
    *written = (uint32_t)((def->offset + end + 3u) / 4u - def->offset / 4u);
    // The fields after the frame's step when it has one.
    const uint32_t head[16] = {(uint32_t)read->field,
                               (uint32_t)read->format,
                               water->added ? water->after : water->now,
                               *written,
                               (uint32_t)read->first[0],
                               (uint32_t)read->first[1],
                               0,
                               0,
                               (uint32_t)read->size[0],
                               (uint32_t)read->size[1],
                               0,
                               bytes,
                               (uint32_t)pitch,
                               (uint32_t)def->offset,
                               (uint32_t)end,
                               0};
    memcpy(words, head, sizeof head);
    mvfxFloatWords(words + 16, read->low, read->high, 0.0f, 0.0f);
    return true;
}

mvfxResult mvfxGpuSim_AddWaterRead(mvfxGpuSim* sim, const mvfxGpuWaterReadDef* def)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_WATER_READ_DEF_COOKIE ||
        def->water < 0 || def->water >= sim->waterCount || def->target.index1 == 0)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    SimWater* water = &sim->waters[def->water];
    if (water->readCount == MVFX_GPU_WATER_READS)
    {
        return mvfx_errorLimit;
    }
    WaterRead* read = &water->reads[water->readCount];
    if (!ReadWords(water, def, read->words, &read->written))
    {
        return mvfx_errorInvalid;
    }
    read->target = def->target;
    bool imported = mrhiImportBuffer(sim->device, read->numbers, &read->resource) == mrhi_success &&
                    Import(sim, water);
    const mrhiResourceId* r = water->resources;
    const mrhiAccess written = {.resource = read->resource, .kind = mrhi_accessCopyDestination};
    const mrhiAccess used[4] = {{.resource = r[WATER_NUMBERS], .kind = mrhi_accessUniform},
                                {.resource = r[WATER_FIELDS], .kind = mrhi_accessStorageRead},
                                {.resource = read->resource, .kind = mrhi_accessUniform},
                                {.resource = def->target, .kind = mrhi_accessStorageReadWrite}};
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = &written;
    pass.accessCount = 1;
    pass.neverCull = true;
    bool added = imported && mrhiAddPass(sim->device, &pass, &read->passes[0]) == mrhi_success;
    pass.passClass = mrhi_passGraphics;
    pass.accesses = used;
    pass.accessCount = 4;
    added = added && mrhiAddPass(sim->device, &pass, &read->passes[1]) == mrhi_success;
    if (!added)
    {
        return mvfx_errorState;
    }
    water->readCount += 1;
    return mvfx_success;
}

int32_t mvfxGpuSim_WaterPasses(const mvfxGpuSim* sim, int32_t water)
{
    if (sim == nullptr || water < 0 || water >= sim->waterCount)
    {
        return 0;
    }
    return 1 + WATER_DISPATCHES;
}

mvfxResult mvfxGpuSim_GetWater(const mvfxGpuSim* sim, int32_t water, mvfxGpuWater* waterOut)
{
    if (waterOut != nullptr)
    {
        *waterOut = (mvfxGpuWater){0};
    }
    if (sim == nullptr || waterOut == nullptr || water < 0 || water >= sim->waterCount)
    {
        return mvfx_errorInvalid;
    }
    *waterOut = (mvfxGpuWater){.statistics = sim->waters[water].buffers[WATER_STATISTICS]};
    return mvfx_success;
}

mvfxResult mvfxGpuSim_ReportWater(const mvfxGpuSim* sim, mvfxContext* context, int32_t water,
                                  const uint32_t* record)
{
    if (sim == nullptr || context == nullptr || context != sim->context || water < 0 ||
        water >= sim->waterCount || record == nullptr)
    {
        return mvfx_errorInvalid;
    }
    uint64_t tick =
        (uint64_t)record[MVFX_GPU_WATER_TICK] | (uint64_t)record[MVFX_GPU_WATER_TICK + 1] << 32u;
    return mvfxInstance_SetWaterHolds(context, sim->instance, water, (int64_t)tick,
                                      record[MVFX_GPU_WATER_HOLDS] != 0);
}
