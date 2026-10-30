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
#define WORD_FACES   16u
#define WORD_PUSHES  20u
#define WORD_SPILLS  (WORD_PUSHES + 4u * MVFX_MAX_WATER_PUSHES)
#define WORD_SOURCES (WORD_SPILLS + 4u * MVFX_MAX_WATER_PUSHES)
#define WORD_TERRAIN (WORD_SOURCES + 8u * MVFX_MAX_WATER_SOURCES)

// The head's flags (water_numbers.wgsl's FLAG_*): shallow water fills at
// this step; it stands on a terrain.
#define FLAG_FILLS   1u
#define FLAG_TERRAIN 2u

// The workgroups of a cell pass.
#define WATER_GROUP 64u

// The crossings buffer's first list (water.wgsl's CROSSINGS_FIRST): after
// the waves' crossings' sum.
#define CROSSINGS_FIRST 4u

// A read's workgroups a row of its dispatch (a device takes 65,535 along
// a dimension; water_read.wgsl's ROW invocations).
#define READ_ROW 32768u

static bool IsShallow(const SimWater* water)
{
    return water->view.def.model == mvfx_waterShallow;
}

mvfxResult mvfxReadSimWaters(mvfxGpuSim* sim, mvfxEffectId effect)
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
    }
    return mvfx_success;
}

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mvfxSimMade(sim, mrhiCreateBuffer(sim->device, &def, out));
}

// Lays out the lists of the emitters coupled to surface w in the crossings
// buffer, in their step order (each a head and two words a particle of its
// capacity); its words in all.
static uint32_t LayCrossings(const mvfxGpuSim* sim, SimWater* water, int32_t w)
{
    uint32_t words = CROSSINGS_FIRST;
    water->couplings = 0;
    for (int32_t n = 0; n < sim->emitterCount; ++n)
    {
        int32_t e = sim->order[n];
        const Emitter* emitter = &sim->emitters[e];
        if (emitter->water == w && emitter->capacity > 0)
        {
            water->coupled[water->couplings] = e;
            water->lists[water->couplings++] = words;
            words += (uint32_t)mvfxCrossingListWords(emitter->capacity);
        }
    }
    return words;
}

bool mvfxCreateSimWater(mvfxGpuSim* sim, SimWater* water, uint32_t hostWords)
{
    const mvfxWaterDef* def = &water->view.def;
    bool shallow = IsShallow(water);
    // Shallow water's terrain: its own grid, or room for the host's.
    static const mvfxGridId noForces[MVFX_MAX_FORCES] = {0};
    mvfxGridId terrain[MVFX_MAX_COLLIDERS] = {0};
    bool own = shallow && def->hasTerrain && !def->hostTerrain;
    terrain[0] = own ? def->terrain.grid : (mvfxGridId){0};
    if (!mvfxLayGrids(sim->context, noForces, terrain, shallow && def->hostTerrain ? hostWords : 0,
                      &water->grids))
    {
        return false;
    }
    uint32_t* w = water->words;
    w[WORD_SIZE] = (uint32_t)def->size[0];
    w[WORD_SIZE + 1] = (uint32_t)def->size[1];
    w[WORD_SIZE + 2] = (uint32_t)water->view.cells;
    w[WORD_SIZE + 3] = def->closedWalls;
    w[WORD_HEAD + 1] = def->model;
    w[WORD_FACES] = (uint32_t)water->view.faces[0];
    w[WORD_FACES + 1] = (uint32_t)water->view.faces[1];
    uint32_t crossings = LayCrossings(sim, water, (int32_t)(water - sim->waters));
    w[WORD_FACES + 2] = (uint32_t)water->couplings;
    mvfxFloatWords(w + WORD_SINK, def->cellSize, 0.0f, def->center[1], 0.0f);
    // Waves: heights and rates, two each; shallow water: depths and two
    // of each axis's faces, the givers' scales and the ground.
    uint64_t floats = 4u * (uint64_t)water->view.cells +
                      2u * (uint64_t)(water->view.faces[0] + water->view.faces[1]);
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiBufferUsage numbers = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    uint32_t gridWords = water->grids.words > 0 ? water->grids.words : 1u;
    bool created =
        CreateBuffer(sim, 4u * MVFX_WATER_WORDS, numbers, &water->buffers[WATER_NUMBERS]) &&
        CreateBuffer(sim, 4u * floats, written | mrhi_bufferCopySource,
                     &water->buffers[WATER_FIELDS]) &&
        CreateBuffer(sim, 4u * MVFX_GPU_WATER_STATISTICS_WORDS, written | mrhi_bufferCopySource,
                     &water->buffers[WATER_STATISTICS]) &&
        CreateBuffer(sim, 4u * gridWords, written, &water->buffers[WATER_GRIDS]) &&
        CreateBuffer(sim, 4u * (uint64_t)crossings, written, &water->buffers[WATER_CROSSINGS]);
    for (int32_t r = 0; created && r < MVFX_GPU_WATER_READS; ++r)
    {
        created = CreateBuffer(sim, 4u * WATER_READ_WORDS, numbers, &water->reads[r].numbers);
    }
    return created && mvfxCreateSimWaterSamples(sim, water);
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
    mvfxDestroySimWaterSamples(sim, water);
}

// Appends a dispatch to the step.
static void Push(SimWater* water, mvfxKernel kernel, uint32_t now, uint32_t groups)
{
    water->dispatches[water->count++] =
        (WaterDispatch){.kernel = kernel, .now = now, .groups = groups};
}

// A waves surface's numbers and dispatches from buffer now, as the core's
// mvfxStepWater takes the plan: the pushes' sinks and the even lift, the
// substeps, the settle; the buffer current after.
static uint32_t PlanWaves(SimWater* water, const mvfxWaterPlan* plan, uint32_t now)
{
    const mvfxWaterDef* def = &water->view.def;
    uint32_t* w = water->words;
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
    // The crossings' sunk water for the lift, then the sinks.
    if (water->couplings > 0)
    {
        Push(water, mvfx_kernelWaterCrossSum, now, 1);
    }
    if (plan->pushCount > 0 || water->couplings > 0)
    {
        Push(water, mvfx_kernelWaterSink, now, groups);
    }
    for (int32_t s = 0; s < plan->substeps; ++s)
    {
        Push(water, mvfx_kernelWaterSubstep, now, groups);
        now = 1u - now;
    }
    Push(water, mvfx_kernelWaterSettle, now, 1);
    return now;
}

// The cells along an axis of n within reach of center, as the core's
// mvfxWaterSpan.
static void Span(float center, float reach, int32_t n, int32_t span[2])
{
    span[0] = (int32_t)fminf(fmaxf(ceilf(center - reach), 0.0f), (float)n);
    span[1] = (int32_t)fminf(fmaxf(floorf(center + reach), -1.0f), (float)(n - 1));
}

// A shallow push's weights over its cells summed, within its radius and
// in its ring, as the core's SpillAt sums them (binary64, cell by cell);
// they hang on its place alone.
static void SpillSums(const mvfxWaterDef* def, const mvfxPlannedWaterPush* push, double sums[2])
{
    const int32_t* n = def->size;
    float h = def->cellSize;
    const float c[2] = {push->at[0] / h + 0.5f * (float)n[0] - 0.5f,
                        push->at[1] / h + 0.5f * (float)n[1] - 0.5f};
    float reach = push->radius / h;
    int32_t xs[2];
    int32_t zs[2];
    Span(c[0], 2.0f * reach, n[0], xs);
    Span(c[1], 2.0f * reach, n[1], zs);
    sums[0] = 0.0;
    sums[1] = 0.0;
    for (int32_t z = zs[0]; z <= zs[1]; ++z)
    {
        for (int32_t x = xs[0]; x <= xs[1]; ++x)
        {
            float dx = (float)x - c[0];
            float dz = (float)z - c[1];
            float r = sqrtf(dx * dx + dz * dz) / reach;
            bool inner = r < 1.0f;
            float s = inner ? 1.0f - r * r : 1.0f - (2.0f * r - 3.0f) * (2.0f * r - 3.0f);
            sums[inner ? 0 : 1] += (double)(r < 2.0f && s > 0.0f ? s * s : 0.0f);
        }
    }
}

// Shallow water's sources' words: each footprint's center and reach, its
// shape (0 a sphere's), the depth it adds this step and its radius.
static void SourceWords(SimWater* water, const mvfxWaterPlan* plan)
{
    const mvfxWaterDef* def = &water->view.def;
    for (int32_t k = 0; k < def->sourceCount; ++k)
    {
        const mvfxWaterSource* source = &def->sources[k];
        bool sphere = source->shape == mvfx_gasSphere;
        uint32_t* at = water->words + WORD_SOURCES + 8u * (uint32_t)k;
        mvfxFloatWords(at, source->center[0], source->center[2],
                       sphere ? source->radius : source->halfExtents[0],
                       sphere ? source->radius : source->halfExtents[2]);
        mvfxFloatWords(at + 4, sphere ? 0.0f : 1.0f, plan->sourceDepths[k], source->radius, 0.0f);
    }
}

// A shallow surface's numbers and dispatches from buffer now, as the
// core's mvfxStepShallow takes the plan: its ground, fill and sources, its
// pushes, three passes a substep, the settle; the buffer current after.
static uint32_t PlanShallow(SimWater* water, const mvfxWaterPlan* plan, uint32_t now)
{
    const mvfxWaterDef* def = &water->view.def;
    uint32_t* w = water->words;
    float dt = plan->dt;
    mvfxFloatWords(w + WORD_FACTORS, def->gravity,
                   (float)mvfxExpMinus((double)(def->friction * dt)), def->fillLevel, dt);
    w[WORD_HEAD] = (uint32_t)plan->pushCount;
    w[WORD_HEAD + 2] = (uint32_t)def->sourceCount;
    w[WORD_HEAD + 3] = (plan->cleared ? FLAG_FILLS : 0u) | (plan->hasTerrain ? FLAG_TERRAIN : 0u);
    // The poured volume into the world's, rounded once.
    float cube = (float)plan->volumeScale;
    memcpy(&w[WORD_SINK + 3u], &cube, sizeof cube);
    for (int32_t p = 0; p < plan->pushCount; ++p)
    {
        const mvfxPlannedWaterPush* push = &plan->pushes[p];
        double sums[2];
        SpillSums(def, push, sums);
        mvfxFloatWords(w + WORD_PUSHES + 4u * (uint32_t)p, push->at[0], push->at[1], push->radius,
                       push->depth);
        mvfxFloatWords(w + WORD_SPILLS + 4u * (uint32_t)p, (float)sums[0], (float)sums[1], 0.0f,
                       0.0f);
    }
    SourceWords(water, plan);
    uint32_t grid[5] = {0};
    if (plan->hasTerrain)
    {
        mvfxColliderGridWords(&water->grids, &plan->terrain, def->hostTerrain ? 0 : 1, 0, grid);
    }
    memset(w + WORD_TERRAIN, 0, MVFX_COLLIDER_WORDS * sizeof(uint32_t));
    if (plan->hasTerrain)
    {
        mvfxColliderWords(&plan->terrain, grid, w + WORD_TERRAIN);
    }
    uint32_t cells = ((uint32_t)water->view.cells + WATER_GROUP - 1u) / WATER_GROUP;
    uint32_t faces = (uint32_t)(water->view.faces[0] + water->view.faces[1]);
    Push(water, mvfx_kernelShallowStart, now, cells);
    if (plan->pushCount > 0 || water->couplings > 0)
    {
        Push(water, mvfx_kernelShallowSpill, now, 1);
    }
    for (int32_t s = 0; s < plan->substeps; ++s)
    {
        Push(water, mvfx_kernelShallowFaces, now, (faces + WATER_GROUP - 1u) / WATER_GROUP);
        Push(water, mvfx_kernelShallowGiving, now, cells);
        Push(water, mvfx_kernelShallowDepths, now, cells);
        now = 1u - now;
    }
    Push(water, mvfx_kernelShallowSettle, now, 1);
    return now;
}

// The step's numbers and dispatches from its plan, its record (nothing
// moving at the step's end), and the buffer current after.
static void Plan(SimWater* water, const mvfxWaterPlan* plan)
{
    water->clears = plan->cleared;
    water->count = 0;
    uint32_t now = plan->cleared ? 0 : water->now;
    now = IsShallow(water) ? PlanShallow(water, plan, now) : PlanWaves(water, plan, now);
    uint64_t end = (uint64_t)(plan->start + plan->ticks);
    water->record[MVFX_GPU_WATER_HOLDS] = 0;
    water->record[MVFX_GPU_WATER_TICK] = (uint32_t)end;
    water->record[MVFX_GPU_WATER_TICK + 1] = (uint32_t)(end >> 32u);
    water->record[MVFX_GPU_WATER_SPEED] = 0;
    water->record[MVFX_GPU_WATER_REACH] = 0;
    water->record[MVFX_GPU_WATER_FACES_HELD] = 0;
    water->record[MVFX_GPU_WATER_POURED] = 0;
    water->after = now;
}

void mvfxSimWaterFields(const SimWater* water, uint32_t now, uint32_t firsts[WATER_COPIES],
                        uint32_t floats[WATER_COPIES])
{
    // The fields' buffer as water.wgsl lays it out.
    uint32_t cells = (uint32_t)water->view.cells;
    uint32_t x = (uint32_t)water->view.faces[0];
    uint32_t z = (uint32_t)water->view.faces[1];
    bool shallow = IsShallow(water);
    const uint32_t first[WATER_COPIES] = {
        now * cells, shallow ? 2u * (cells + x + z) + cells : (2u + now) * cells,
        2u * cells + now * x, 2u * cells + 2u * x + now * z};
    const uint32_t count[WATER_COPIES] = {cells, cells, shallow ? x : 0u, shallow ? z : 0u};
    memcpy(firsts, first, sizeof first);
    memcpy(floats, count, sizeof count);
}

bool mvfxImportSimWater(mvfxGpuSim* sim, SimWater* water)
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
    // The reads and samples of a frame the host dropped go.
    water->added = false;
    water->readCount = 0;
    water->sampleCount = 0;
    water->imported = false;
    mvfxWaterPlan plan;
    mvfxResult result = mvfxSimWaterPlan(sim, w, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    // A host's terrain's grid into the room for it.
    if (IsShallow(water) && water->view.def.hostTerrain && plan.hasTerrain &&
        !mvfxPlaceHostGrids(sim->context, &plan.terrain, 1, 0, &water->grids))
    {
        return mvfx_errorCapacity;
    }
    Plan(water, &plan);
    if (!mvfxImportSimWater(sim, water))
    {
        return mvfx_errorState;
    }
    const mrhiResourceId* r = water->resources;
    // The write pass copies in the coupled emitters' lists.
    mrhiAccess written[WATER_BUFFERS + MVFX_MAX_EMITTERS] = {
        {.resource = r[WATER_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = r[WATER_FIELDS], .kind = mrhi_accessCopyDestination},
        {.resource = r[WATER_STATISTICS], .kind = mrhi_accessCopyDestination},
        {.resource = r[WATER_GRIDS], .kind = mrhi_accessCopyDestination},
        {.resource = r[WATER_CROSSINGS], .kind = mrhi_accessCopyDestination}};
    for (int32_t k = 0; k < water->couplings; ++k)
    {
        written[WATER_BUFFERS + k] =
            (mrhiAccess){.resource = sim->emitters[water->coupled[k]].resources[BUFFER_EVENTS],
                         .kind = mrhi_accessCopySource};
    }
    const mrhiAccess used[WATER_BUFFERS] = {
        {.resource = r[WATER_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[WATER_FIELDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[WATER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[WATER_GRIDS], .kind = mrhi_accessStorageRead},
        {.resource = r[WATER_CROSSINGS], .kind = mrhi_accessStorageReadWrite}};
    for (int32_t p = -1; p < water->count; ++p)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p < 0 ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p < 0 ? written : used;
        def.accessCount = p < 0 ? WATER_BUFFERS + water->couplings : WATER_BUFFERS;
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

// Records the copies of the coupled emitters' crossings' lists, each its
// room whole (its count the device's).
static bool CopyCrossings(mvfxGpuSim* sim, mrhiPassId pass, const SimWater* water)
{
    bool copied = true;
    for (int32_t k = 0; copied && k < water->couplings; ++k)
    {
        const Emitter* emitter = &sim->emitters[water->coupled[k]];
        copied =
            mrhiPass_CopyBuffer(sim->device, pass, emitter->resources[BUFFER_EVENTS],
                                4u * (uint64_t)emitter->crossingList,
                                water->resources[WATER_CROSSINGS], 4u * (uint64_t)water->lists[k],
                                4u * mvfxCrossingListWords(emitter->capacity)) == mrhi_success;
    }
    return copied;
}

// Records the pass writing the numbers (waves' up to their pushes, the
// rest shallow water's), the record and the terrain's grid when it
// changed, the coupled emitters' crossings, and the fields emptied after a
// clear.
static bool RecordWrite(mvfxGpuSim* sim, SimWater* water)
{
    mrhiPassId pass = water->passes[0];
    const mrhiResourceId* r = water->resources;
    uint64_t words = IsShallow(water) ? MVFX_WATER_WORDS : WORD_SPILLS;
    bool recorded = mrhiPass_Begin(sim->device, pass) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, pass, r[WATER_NUMBERS], 0, water->words,
                               4u * words) &&
                    mvfxUpload(sim->device, &sim->uploaded, pass, r[WATER_STATISTICS], 0,
                               water->record, sizeof water->record) &&
                    mvfxWriteGrids(sim->device, &sim->uploaded, pass, r[WATER_GRIDS], sim->context,
                                   &water->grids) &&
                    CopyCrossings(sim, pass, water);
    if (recorded && water->clears)
    {
        recorded = mrhiPass_ClearBuffer(sim->device, pass, r[WATER_FIELDS], 0, MRHI_WHOLE_SIZE) ==
                   mrhi_success;
    }
    return mrhiPass_End(sim->device, pass) == mrhi_success && recorded;
}

// Records the step's dispatches.
static bool RecordStep(mvfxGpuSim* sim, SimWater* water)
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
        bool ran =
            mrhiPass_Begin(sim->device, pass) == mrhi_success &&
            mrhiPass_SetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, d->kernel)) ==
                mrhi_success &&
            mrhiPass_SetBindings(sim->device, pass, 0, bindings, WATER_BUFFERS) == mrhi_success &&
            mrhiPass_SetRootBlock(sim->device, pass, 0, root, sizeof root) == mrhi_success &&
            mrhiPass_Dispatch(sim->device, pass, d->groups, 1, 1) == mrhi_success;
        recorded = mrhiPass_End(sim->device, pass) == mrhi_success && ran;
    }
    return recorded;
}

// Records a read's passes: its numbers' write, then its words.
static bool RecordRead(mvfxGpuSim* sim, const SimWater* water, const WaterRead* read)
{
    bool recorded = mrhiPass_Begin(sim->device, read->passes[0]) == mrhi_success &&
                    mvfxUpload(sim->device, &sim->uploaded, read->passes[0], read->resource, 0,
                               read->words, sizeof read->words);
    recorded = mrhiPass_End(sim->device, read->passes[0]) == mrhi_success && recorded;
    const mrhiBinding bindings[4] = {
        {.slot = 0, .resource = water->resources[WATER_NUMBERS], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = water->resources[WATER_FIELDS], .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = read->resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 3, .resource = read->target, .size = MRHI_WHOLE_SIZE}};
    mrhiPassId pass = read->passes[1];
    uint32_t groups = (read->written + WATER_GROUP - 1u) / WATER_GROUP;
    bool ran =
        recorded && mrhiPass_Begin(sim->device, pass) == mrhi_success &&
        mrhiPass_SetComputePipeline(
            sim->device, pass, mvfxGpuPipeline(sim->gpu, mvfx_kernelWaterRead)) == mrhi_success &&
        mrhiPass_SetBindings(sim->device, pass, 0, bindings, 4) == mrhi_success &&
        (groups == 0 || mrhiPass_Dispatch(sim->device, pass, groups < READ_ROW ? groups : READ_ROW,
                                          (groups + READ_ROW - 1u) / READ_ROW, 1) == mrhi_success);
    return mrhiPass_End(sim->device, pass) == mrhi_success && ran;
}

bool mvfxRecordSimWater(mvfxGpuSim* sim, SimWater* water)
{
    bool recorded = !water->added || RecordStep(sim, water);
    water->stepped = water->stepped || (water->added && recorded);
    water->now = water->added ? water->after : water->now;
    water->added = false;
    for (int32_t r = 0; recorded && r < water->readCount; ++r)
    {
        recorded = RecordRead(sim, water, &water->reads[r]);
    }
    recorded = recorded && mvfxRecordSimWaterSamples(sim, water);
    water->readCount = 0;
    water->sampleCount = 0;
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
        return mvfx_errorBusy;
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
                    mvfxImportSimWater(sim, water);
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
    // The write, then waves' crossings' sum, sinks, substeps and settle,
    // or shallow water's start, pushes, three passes a substep and settle.
    return IsShallow(&sim->waters[water]) ? 3 + 3 * MVFX_MAX_WATER_SUBSTEPS + 1
                                          : 4 + MVFX_MAX_WATER_SUBSTEPS;
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
    mvfxResult result = mvfxInstance_SetWaterHolds(context, sim->instance, water, (int64_t)tick,
                                                   record[MVFX_GPU_WATER_HOLDS] != 0);
    float reach;
    float poured;
    memcpy(&reach, &record[MVFX_GPU_WATER_REACH], sizeof reach);
    memcpy(&poured, &record[MVFX_GPU_WATER_POURED], sizeof poured);
    result = result == mvfx_success
                 ? mvfxInstance_SetWaterCounts(context, sim->instance, water, (int64_t)tick, reach,
                                               record[MVFX_GPU_WATER_FACES_HELD], (double)poured)
                 : result;
    if (result != mvfx_success || !IsShallow(&sim->waters[water]))
    {
        return result;
    }
    float speed;
    memcpy(&speed, &record[MVFX_GPU_WATER_SPEED], sizeof speed);
    return mvfxInstance_SetWaterSpeed(context, sim->instance, water, (int64_t)tick, speed);
}
