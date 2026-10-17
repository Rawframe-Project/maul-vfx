// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance on the GPU: each emitter's buffers, the step's numbers
// it writes from the plan and the emitter's view, and the five passes of
// its step (sim.wgsl).

#include "maul-vfx-rhi/sim.h"

#include "allocator.h"
#include "kernels.h"
#include "numbers.h"
#include "record.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

#include <stdalign.h>
#include <string.h>

// The buffers of the step an emitter has: its numbers, its births, its
// particles in the buffer they are in and the other, its statistics
// record, which births have room, the scan's blocks and their boxes.
enum
{
    BUFFER_STEP = 0,
    BUFFER_BIRTHS = 1,
    BUFFER_CURRENT = 2,
    BUFFER_OTHER = 3,
    BUFFER_STATISTICS = 4,
    BUFFER_ACCEPTED = 5,
    BUFFER_BLOCKS = 6,
    BUFFER_COUNT = 7
};

// The passes of an emitter's step: the plan's bytes, then the kernels.
enum
{
    PASS_WRITE = 0,
    PASS_UPDATE = 1,
    PASS_ROOM = 2,
    PASS_KILL = 3,
    PASS_REDUCE = 4,
    PASS_OFFSETS = 5,
    PASS_SCATTER = 6,
    PASS_MEASURE = 7,
    PASS_COUNT = 8
};

typedef struct Emitter
{
    int32_t capacity;
    mrhiBufferId particles[2];
    mrhiBufferId births;
    mrhiBufferId step;
    mrhiBufferId statistics;
    mrhiBufferId accepted;
    mrhiBufferId blocks;
    // The particle buffer the last recorded step left them in.
    int32_t current;
    // The step added and not yet recorded: its passes, the buffers as the
    // frame has them, its numbers, its map for the record and the plan's
    // births.
    bool added;
    mrhiPassId passes[PASS_COUNT];
    mrhiResourceId resources[BUFFER_COUNT];
    uint32_t words[MVFX_STEP_WORDS];
    uint32_t map[MVFX_RECORD_MAP_WORDS];
    const mvfxPlannedBirth* plannedBirths;
    int32_t birthCount;
    bool cleared;
} Emitter;

struct mvfxGpuSim
{
    mvfxAllocator allocator;
    mvfxGpu* gpu;
    mrhiDevice* device;
    const mvfxContext* context;
    mvfxInstanceId instance;
    int32_t emitterCount;
    mvfxEmitterView views[MVFX_MAX_EMITTERS];
    Emitter emitters[MVFX_MAX_EMITTERS];
};

mvfxGpuSimDef mvfxDefaultGpuSimDef(void)
{
    return (mvfxGpuSimDef){.cookie = MVFX_GPU_SIM_DEF_COOKIE};
}

// The features the part runs, and the kinds of force: constant, wind,
// attractor, vortex, turbulence.
#define RUN_FEATURES (MVFX_FEATURE_INHERIT_VELOCITY | MVFX_FEATURE_FORCES)
#define RUN_FORCES   0x1Fu

// Whether the part runs an emitter; what it does not, in refusal.
static bool Runs(const mvfxEmitterView* view, mvfxGpuRefusal* refusal)
{
    const mvfxVelocityView* v = &view->velocity;
    *refusal = (mvfxGpuRefusal){
        .features = (view->features & ~RUN_FEATURES) |
                    ((view->forceKinds & ~RUN_FORCES) != 0 ? MVFX_FEATURE_FORCES : 0),
        .shape = view->shape.kind != mvfx_shapeSphere,
        .velocity = (v->mode != mvfx_directionShapeNormal && v->mode != mvfx_directionCone &&
                     v->mode != mvfx_directionFixed) ||
                    v->randomize != 0.0f,
    };
    return refusal->features == 0 && !refusal->shape && !refusal->velocity;
}

// Reads the effect's emitters' views; false with a status when one is
// gone or not run.
static mvfxResult ReadViews(mvfxGpuSim* sim, mvfxEffectId effect, mvfxGpuRefusal* refusal)
{
    for (int32_t e = 0; e < MVFX_MAX_EMITTERS; ++e)
    {
        mvfxResult result = mvfxEffect_GetEmitterView(sim->context, effect, e, &sim->views[e]);
        if (result == mvfx_errorInvalid)
        {
            break;
        }
        if (result != mvfx_success)
        {
            return result;
        }
        sim->emitterCount = e + 1;
        if (!Runs(&sim->views[e], refusal))
        {
            refusal->emitter = e;
            return mvfx_errorState;
        }
    }
    *refusal = (mvfxGpuRefusal){.emitter = -1};
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

// Creates an emitter's buffers for its capacity.
static bool CreateBuffers(mvfxGpuSim* sim, Emitter* emitter)
{
    uint64_t c = (uint64_t)emitter->capacity;
    mrhiBufferUsage particles = mrhi_bufferStorage | mrhi_bufferCopySource;
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    return CreateBuffer(sim, c * MVFX_GPU_PARTICLE_WORDS * 4u, particles, &emitter->particles[0]) &&
           CreateBuffer(sim, c * MVFX_GPU_PARTICLE_WORDS * 4u, particles, &emitter->particles[1]) &&
           CreateBuffer(sim, 2u * c * sizeof(mvfxPlannedBirth), written, &emitter->births) &&
           CreateBuffer(sim, MVFX_STEP_WORDS * 4u, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                        &emitter->step) &&
           CreateBuffer(sim, MVFX_GPU_STATISTICS_WORDS * 4u, written | mrhi_bufferCopySource,
                        &emitter->statistics) &&
           CreateBuffer(sim, 2u * c * sizeof(uint32_t), mrhi_bufferStorage, &emitter->accepted) &&
           CreateBuffer(sim, 7u * mvfxScanBlocks(emitter->capacity) * sizeof(uint32_t),
                        mrhi_bufferStorage, &emitter->blocks);
}

// Whether a def is one a simulation can be made from.
static bool IsDefValid(const mvfxGpuSimDef* def)
{
    return def->cookie == MVFX_GPU_SIM_DEF_COOKIE && def->context != nullptr &&
           mvfxGpu_IsReady(def->gpu) && mvfxRhiIsAllocatorValid(&def->allocator);
}

mvfxResult mvfxCreateGpuSim(const mvfxGpuSimDef* def, mvfxGpuSim** simOut,
                            mvfxGpuRefusal* refusalOut)
{
    mvfxGpuRefusal refusal = {.emitter = -1};
    if (simOut != nullptr)
    {
        *simOut = nullptr;
    }
    if (refusalOut != nullptr)
    {
        *refusalOut = refusal;
    }
    if (def == nullptr || simOut == nullptr || !IsDefValid(def))
    {
        return mvfx_errorInvalid;
    }
    mvfxEmitterPlan plan;
    mvfxResult planned = mvfxInstance_GetEmitterPlan(def->context, def->instance, 0, &plan);
    if (planned != mvfx_success)
    {
        return planned == mvfx_errorState ? mvfx_errorInvalid : planned;
    }
    mvfxGpuSim* sim = mvfxRhiAllocate(&def->allocator, sizeof *sim, alignof(mvfxGpuSim));
    if (sim == nullptr)
    {
        return mvfx_errorCapacity;
    }
    *sim = (mvfxGpuSim){
        .allocator = def->allocator,
        .gpu = def->gpu,
        .device = mvfxGpuDevice(def->gpu),
        .context = def->context,
        .instance = def->instance,
    };
    mvfxResult result = ReadViews(sim, def->effect, &refusal);
    for (int32_t e = 0; result == mvfx_success && e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        result = mvfxInstance_GetEmitterPlan(def->context, def->instance, e, &plan);
        emitter->capacity = plan.capacity;
        if (result == mvfx_success && emitter->capacity > 0 && !CreateBuffers(sim, emitter))
        {
            result = mvfx_errorState;
        }
    }
    if (result != mvfx_success)
    {
        mvfxDestroyGpuSim(sim);
        if (refusalOut != nullptr)
        {
            *refusalOut = refusal;
        }
        return result;
    }
    *simOut = sim;
    return mvfx_success;
}

// Destroys a buffer the simulation made; a null id was never made.
static void DestroyBuffer(mvfxGpuSim* sim, mrhiBufferId buffer)
{
    if (buffer.index1 != 0)
    {
        (void)mrhiDestroyBuffer(sim->device, buffer);
    }
}

void mvfxDestroyGpuSim(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return;
    }
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        DestroyBuffer(sim, emitter->particles[0]);
        DestroyBuffer(sim, emitter->particles[1]);
        DestroyBuffer(sim, emitter->births);
        DestroyBuffer(sim, emitter->step);
        DestroyBuffer(sim, emitter->statistics);
        DestroyBuffer(sim, emitter->accepted);
        DestroyBuffer(sim, emitter->blocks);
    }
    mvfxAllocator allocator = sim->allocator;
    mvfxRhiRelease(&allocator, sim, sizeof *sim, alignof(mvfxGpuSim));
}

mvfxResult mvfxGpuSim_GetParticles(const mvfxGpuSim* sim, int32_t emitter,
                                   mvfxGpuParticles* particlesOut)
{
    if (particlesOut != nullptr)
    {
        *particlesOut = (mvfxGpuParticles){0};
    }
    if (sim == nullptr || particlesOut == nullptr || emitter < 0 || emitter >= sim->emitterCount)
    {
        return mvfx_errorInvalid;
    }
    const Emitter* e = &sim->emitters[emitter];
    *particlesOut = (mvfxGpuParticles){
        .particles = e->particles[e->current],
        .statistics = e->statistics,
        .capacity = e->capacity,
    };
    return mvfx_success;
}

// Adds an emitter's passes: one writing the plan's bytes, one a kernel.
static bool AddEmitterPasses(mvfxGpuSim* sim, Emitter* emitter)
{
    const mrhiBufferId buffers[BUFFER_COUNT] = {emitter->step,
                                                emitter->births,
                                                emitter->particles[emitter->current],
                                                emitter->particles[1 - emitter->current],
                                                emitter->statistics,
                                                emitter->accepted,
                                                emitter->blocks};
    for (int32_t b = 0; b < BUFFER_COUNT; ++b)
    {
        if (mrhiImportBuffer(sim->device, buffers[b], &emitter->resources[b]) != mrhi_success)
        {
            return false;
        }
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written[3] = {
        {.resource = r[BUFFER_STEP], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_BIRTHS], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[BUFFER_COUNT] = {
        {.resource = r[BUFFER_STEP], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_BIRTHS], .kind = mrhi_accessStorageRead},
        {.resource = r[BUFFER_CURRENT], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_ACCEPTED], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_BLOCKS], .kind = mrhi_accessStorageReadWrite}};
    for (int32_t p = 0; p < PASS_COUNT; ++p)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p == PASS_WRITE ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p == PASS_WRITE ? written : used;
        def.accessCount = p == PASS_WRITE ? 3 : BUFFER_COUNT;
        // Its writes outlive the frame, read by the next step or the host.
        def.neverCull = true;
        if (mrhiAddPass(sim->device, &def, &emitter->passes[p]) != mrhi_success)
        {
            return false;
        }
    }
    return true;
}

mvfxResult mvfxGpuSim_AddPasses(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        emitter->added = false;
        mvfxEmitterPlan plan;
        mvfxResult result = mvfxInstance_GetEmitterPlan(sim->context, sim->instance, e, &plan);
        if (result != mvfx_success)
        {
            return result;
        }
        if (emitter->capacity == 0)
        {
            continue;
        }
        mvfxStepNumbers(&sim->views[e], &plan, e, emitter->words);
        mvfxRecordMap(&sim->views[e], &plan, emitter->map);
        emitter->plannedBirths = plan.births;
        emitter->birthCount = plan.birthCount;
        emitter->cleared = plan.cleared;
        if (!AddEmitterPasses(sim, emitter))
        {
            return mvfx_errorState;
        }
        emitter->added = true;
    }
    return mvfx_success;
}

// Records the pass writing the plan's bytes: its numbers, its births, the
// record's map, and the counts emptied after a clear.
static bool RecordWrites(mvfxGpuSim* sim, const Emitter* emitter)
{
    static const uint32_t empty[MVFX_RECORD_PAST + 1] = {0};
    mrhiPassId pass = emitter->passes[PASS_WRITE];
    const mrhiResourceId* r = emitter->resources;
    bool recorded = mrhiBeginPass(sim->device, pass) == mrhi_success &&
                    mrhiWriteBuffer(sim->device, pass, r[BUFFER_STEP], 0, emitter->words,
                                    sizeof emitter->words) == mrhi_success &&
                    (emitter->birthCount == 0 ||
                     mrhiWriteBuffer(sim->device, pass, r[BUFFER_BIRTHS], 0, emitter->plannedBirths,
                                     (uint64_t)emitter->birthCount * sizeof(mvfxPlannedBirth)) ==
                         mrhi_success) &&
                    mrhiWriteBuffer(sim->device, pass, r[BUFFER_STATISTICS],
                                    MVFX_RECORD_SPACE * sizeof(uint32_t), emitter->map,
                                    sizeof emitter->map) == mrhi_success &&
                    (!emitter->cleared || mrhiWriteBuffer(sim->device, pass, r[BUFFER_STATISTICS],
                                                          0, empty, sizeof empty) == mrhi_success);
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records a kernel's pass over the emitter's buffers.
static bool RecordKernel(mvfxGpuSim* sim, const Emitter* emitter, int32_t p, mvfxKernel kernel,
                         uint32_t groups)
{
    mrhiPassId pass = emitter->passes[p];
    mrhiBinding bindings[BUFFER_COUNT];
    for (int32_t b = 0; b < BUFFER_COUNT; ++b)
    {
        bindings[b] = (mrhiBinding){
            .slot = (uint32_t)b, .resource = emitter->resources[b], .size = MRHI_WHOLE_SIZE};
    }
    bool recorded = mrhiBeginPass(sim->device, pass) == mrhi_success &&
                    mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, kernel)) ==
                        mrhi_success &&
                    mrhiSetBindings(sim->device, pass, 0, bindings, BUFFER_COUNT) == mrhi_success &&
                    mrhiDispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        if (!emitter->added)
        {
            continue;
        }
        emitter->added = false;
        uint32_t groups = mvfxUpdateGroups(emitter->capacity);
        uint32_t blocks = mvfxScanBlocks(emitter->capacity);
        bool recorded = RecordWrites(sim, emitter) &&
                        RecordKernel(sim, emitter, PASS_UPDATE, mvfx_kernelUpdate, groups) &&
                        RecordKernel(sim, emitter, PASS_ROOM, mvfx_kernelRoom, 1) &&
                        RecordKernel(sim, emitter, PASS_KILL, mvfx_kernelKillBirths, groups) &&
                        RecordKernel(sim, emitter, PASS_REDUCE, mvfx_kernelReduce, blocks) &&
                        RecordKernel(sim, emitter, PASS_OFFSETS, mvfx_kernelOffsets, 1) &&
                        RecordKernel(sim, emitter, PASS_SCATTER, mvfx_kernelScatter, blocks) &&
                        RecordKernel(sim, emitter, PASS_MEASURE, mvfx_kernelMeasure, 1);
        if (!recorded)
        {
            return mvfx_errorState;
        }
        // The step's particles are in the other buffer now.
        emitter->current = 1 - emitter->current;
    }
    return mvfx_success;
}

mvfxResult mvfxGpuSim_BoundsOf(const mvfxGpuSim* sim, int32_t emitter, const uint32_t* record,
                               mvfxBounds* boundsOut)
{
    if (boundsOut != nullptr)
    {
        *boundsOut = (mvfxBounds){.empty = true};
    }
    if (sim == nullptr || record == nullptr || boundsOut == nullptr || emitter < 0 ||
        emitter >= sim->emitterCount)
    {
        return mvfx_errorInvalid;
    }
    *boundsOut = mvfxRecordBounds(&sim->views[emitter], record);
    return mvfx_success;
}
