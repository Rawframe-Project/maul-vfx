// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance on the GPU: each emitter's buffers, the step's numbers
// it writes from the plan and the emitter's view, and the five passes of
// its step (sim.wgsl).

#include "maul-vfx-rhi/sim.h"

#include "allocator.h"
#include "grids.h"
#include "kernels.h"
#include "numbers.h"
#include "record.h"
#include "records.h"
#include "sim_state.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

// The parts of the depth and normal textures the kernels read.
static const mrhiTextureRange TEXTURE_RANGES[2] = {
    {.mipCount = 1, .layerCount = 1, .aspect = mrhi_aspectDepthOnly},
    {.mipCount = 1, .layerCount = 1, .aspect = mrhi_aspectAll}};

mvfxGpuSimDef mvfxDefaultGpuSimDef(void)
{
    return (mvfxGpuSimDef){.cookie = MVFX_GPU_SIM_DEF_COOKIE};
}

// The features the part runs (values over life are applied where
// particles are read, but for the rotation's speed, which it runs), the
// kinds of force (constant, wind, attractor, vortex, turbulence, vector
// field) and of collider (plane, sphere, box, heightfield, distance grid).
#define RUN_FEATURES                                                                               \
    (MVFX_FEATURE_INHERIT_VELOCITY | MVFX_FEATURE_FORCES | MVFX_FEATURE_COLLISION |                \
     MVFX_FEATURE_SIZE | MVFX_FEATURE_COLOR | MVFX_FEATURE_CUSTOM | MVFX_FEATURE_FRAME |           \
     MVFX_FEATURE_MESH_INDEX | MVFX_FEATURE_OVER_LIFE | MVFX_FEATURE_ANGLE |                       \
     MVFX_FEATURE_ORIENTATION | MVFX_FEATURE_DRAG_SCALE | MVFX_FEATURE_VELOCITY_MODULES |          \
     MVFX_FEATURE_KILL_RULES | MVFX_FEATURE_RATE_CURVE | MVFX_FEATURE_DISTANCE |                   \
     MVFX_FEATURE_HOST_SPAWN | MVFX_FEATURE_SORT | MVFX_FEATURE_EVENTS)
#define RUN_FORCES    0x3Fu
#define RUN_COLLIDERS 0x1Fu

// Whether the part runs an emitter; what it does not, in refusal.
static bool Runs(const mvfxEmitterView* view, mvfxGpuRefusal* refusal)
{
    const mvfxVelocityView* v = &view->velocity;
    *refusal = (mvfxGpuRefusal){
        .features = (view->features & ~RUN_FEATURES) |
                    ((view->forceKinds & ~RUN_FORCES) != 0 ? MVFX_FEATURE_FORCES : 0) |
                    ((view->colliderKinds & ~RUN_COLLIDERS) != 0 || view->sceneQueries
                         ? MVFX_FEATURE_COLLISION
                         : 0),
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

// The bytes of an emitter's births, and of its host queue's after them.
static uint64_t BirthBytes(const Emitter* emitter)
{
    return 2u * (uint64_t)emitter->capacity * sizeof(mvfxPlannedBirth);
}

static uint64_t HostBytes(const Emitter* emitter)
{
    return (uint64_t)emitter->hostCapacity * MVFX_HOST_BIRTH_WORDS * 4u;
}

// Creates an emitter's buffers for its capacity.
static bool CreateBuffers(mvfxGpuSim* sim, Emitter* emitter)
{
    uint64_t c = (uint64_t)emitter->capacity;
    mrhiBufferUsage particles = mrhi_bufferStorage | mrhi_bufferCopySource;
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    uint64_t words = (uint64_t)emitter->particleWords;
    return CreateBuffer(sim, c * words * 4u, particles, &emitter->particles[0]) &&
           CreateBuffer(sim, c * words * 4u, particles, &emitter->particles[1]) &&
           CreateBuffer(sim, BirthBytes(emitter) + HostBytes(emitter), written, &emitter->births) &&
           CreateBuffer(sim, MVFX_STEP_WORDS * 4u, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                        &emitter->step) &&
           CreateBuffer(sim, MVFX_GPU_STATISTICS_WORDS * 4u, written | mrhi_bufferCopySource,
                        &emitter->statistics) &&
           CreateBuffer(sim, 2u * c * sizeof(uint32_t), mrhi_bufferStorage, &emitter->accepted) &&
           CreateBuffer(sim, 8u * mvfxScanBlocks(emitter->capacity) * sizeof(uint32_t),
                        mrhi_bufferStorage, &emitter->blocks) &&
           CreateBuffer(sim, 4u * (emitter->gridLayout.words > 0 ? emitter->gridLayout.words : 1u),
                        written, &emitter->grids);
}

// Creates an emitter's sort buffers when it sorts by place: keys and
// indices twice over its blocks, and two sets of digit counts.
static bool CreateSortBuffers(mvfxGpuSim* sim, const mvfxEmitterView* view, Emitter* emitter)
{
    if (mvfxRecordOrder(view) != MVFX_ORDER_SORTED)
    {
        return true;
    }
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    return CreateBuffer(sim, 16u * blocks * MVFX_SORT_BLOCK, mrhi_bufferStorage, &emitter->sort) &&
           CreateBuffer(sim, 2u * 256u * blocks * 4u, mrhi_bufferStorage, &emitter->digits);
}

// Creates an emitter's events buffer when it writes events.
static bool CreateEventBuffer(mvfxGpuSim* sim, const mvfxEmitterView* view, Emitter* emitter)
{
    if (view->events == 0)
    {
        return true;
    }
    return CreateBuffer(sim, 4u * mvfxEventBufferWords(emitter->capacity, view->eventCapacity),
                        mrhi_bufferStorage | mrhi_bufferCopySource, &emitter->events) &&
           CreateBuffer(sim, MVFX_RECORD_WORDS * 4u,
                        mrhi_bufferUniform | mrhi_bufferCopyDestination, &emitter->eventNumbers);
}

// Creates the simulation's own 1-pixel textures.
static bool CreateTextures(mvfxGpuSim* sim)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.width = 1;
    def.height = 1;
    def.usage = mrhi_textureSampled;
    def.format = mrhi_formatDepth32Float;
    bool made = mrhiCreateTexture(sim->device, &def, &sim->ownDepth) == mrhi_success;
    def.format = mrhi_formatRgba32Float;
    return made && mrhiCreateTexture(sim->device, &def, &sim->ownNormals) == mrhi_success;
}

static_assert(MVFX_GPU_EVENT_WORDS == MVFX_EVENT_WORDS, "an event's words, as the kernel's");
static_assert(MVFX_GPU_EVENTS_WRITTEN == MVFX_RECORD_EVENTS, "the events' count, as the record's");

static_assert(MVFX_GPU_TABLES >= 2 * MVFX_GPU_SOURCES + MVFX_GPU_LIVES + 1 + MVFX_RECORD_LIVES,
              "room for every table");

// Places the baked tables of an emitter's sources and values over life,
// its step's and its records', after its grids.
static void LayTables(const mvfxEmitterView* view, Emitter* emitter)
{
    mvfxGpuGrids* layout = &emitter->gridLayout;
    mvfxSourceView sources[MVFX_GPU_SOURCES];
    mvfxSourcesOf(view, sources);
    for (int32_t s = 0; s < MVFX_GPU_SOURCES; ++s)
    {
        emitter->tables.sources[s][0] =
            mvfxAddTable(layout, sources[s].curve, MVFX_CURVE_SAMPLES + 2);
        emitter->tables.sources[s][1] =
            mvfxAddTable(layout, sources[s].gradient, 4 * MVFX_CURVE_SAMPLES);
    }
    mvfxLifeView lives[MVFX_GPU_LIVES];
    mvfxLivesOf(view, lives);
    for (int32_t k = 0; k < MVFX_GPU_LIVES; ++k)
    {
        emitter->tables.lives[k] = mvfxAddTable(layout, lives[k].curve, MVFX_CURVE_SAMPLES + 2);
    }
    emitter->tables.gradient =
        mvfxAddTable(layout, view->overLife.gradient, 4 * MVFX_CURVE_SAMPLES);
    mvfxLifeView recordLives[MVFX_RECORD_LIVES];
    mvfxRecordLivesOf(view, recordLives);
    for (int32_t k = 0; k < MVFX_RECORD_LIVES; ++k)
    {
        emitter->tables.recordLives[k] =
            mvfxAddTable(layout, recordLives[k].curve, MVFX_CURVE_SAMPLES + 2);
    }
}

// Sizes an emitter's memory by its plan and makes it: the host's
// particles' words, the grids and tables laid out, the buffers.
static mvfxResult PrepareEmitter(mvfxGpuSim* sim, const mvfxGpuSimDef* def, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    mvfxEmitterPlan plan;
    mvfxResult result = mvfxInstance_GetEmitterPlan(def->context, def->instance, e, &plan);
    emitter->capacity = plan.capacity;
    emitter->hostCapacity = plan.capacity > 0 ? sim->views[e].hostSpawnCapacity : 0;
    emitter->particleWords = sim->views[e].arrays.count + MVFX_GPU_OWN_WORDS;
    if (result == mvfx_success && emitter->hostCapacity > 0)
    {
        emitter->hostWords = mvfxRhiAllocate(&sim->allocator, HostBytes(emitter), 4);
        result = emitter->hostWords != nullptr ? mvfx_success : mvfx_errorCapacity;
    }
    uint32_t hostWords = (uint32_t)((def->hostGridBytes + 3) / 4);
    if (result == mvfx_success &&
        !mvfxLayGrids(def->context, &sim->views[e], hostWords, &emitter->gridLayout))
    {
        result = mvfx_errorStale;
    }
    if (result == mvfx_success)
    {
        LayTables(&sim->views[e], emitter);
    }
    if (result == mvfx_success && emitter->capacity > 0 &&
        (!CreateBuffers(sim, emitter) || !mvfxCreateRecordBuffers(sim->device, &emitter->records) ||
         !CreateSortBuffers(sim, &sim->views[e], emitter) ||
         !CreateEventBuffer(sim, &sim->views[e], emitter)))
    {
        result = mvfx_errorState;
    }
    return result;
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
        result = PrepareEmitter(sim, def, e);
    }
    mvfxSrgbSteps(sim->srgb);
    if (result == mvfx_success &&
        (!CreateTextures(sim) || !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownCount) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownSort) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownDigits) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownEvents)))
    {
        result = mvfx_errorState;
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
        DestroyBuffer(sim, emitter->grids);
        mvfxDestroyRecordBuffers(sim->device, &emitter->records);
        DestroyBuffer(sim, emitter->sort);
        DestroyBuffer(sim, emitter->digits);
        DestroyBuffer(sim, emitter->events);
        DestroyBuffer(sim, emitter->eventNumbers);
        mvfxRhiRelease(&sim->allocator, emitter->hostWords, HostBytes(emitter), 4);
    }
    DestroyBuffer(sim, sim->ownCount);
    DestroyBuffer(sim, sim->ownSort);
    DestroyBuffer(sim, sim->ownDigits);
    DestroyBuffer(sim, sim->ownEvents);
    mrhiTextureId textures[2] = {sim->ownDepth, sim->ownNormals};
    for (int32_t t = 0; t < 2; ++t)
    {
        if (textures[t].index1 != 0)
        {
            (void)mrhiDestroyTexture(sim->device, textures[t]);
        }
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
        .words = e->particleWords,
        .events = e->events,
        .eventFirst = e->events.index1 != 0 ? mvfxEventFirst(e->capacity) : 0,
        .eventCapacity = e->events.index1 != 0 ? sim->views[emitter].eventCapacity : 0,
    };
    return mvfx_success;
}

// An emitter's events buffer, or the simulation's word for one that
// writes none.
static mrhiBufferId EventsOf(const mvfxGpuSim* sim, const Emitter* emitter)
{
    return emitter->events.index1 != 0 ? emitter->events : sim->ownEvents;
}

// The numbers of an emitter's events' values over life: a record pass's
// over its events, no field written, its workgroups striding over them.
static void EventNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (emitter->events.index1 == 0)
    {
        return;
    }
    static const mvfxLayoutView none = {0};
    mvfxGpuRecordsDef def = mvfxDefaultGpuRecordsDef();
    def.emitter = e;
    def.offset = 4u * mvfxEventFirst(emitter->capacity);
    def.capacity = sim->views[e].eventCapacity;
    mvfxRecordNumbers(&sim->views[e], plan, &none, &def, &emitter->tables, sim->srgb,
                      emitter->eventWords);
    uint32_t groups = ((uint32_t)def.capacity + 63u) / 64u;
    emitter->eventWords[5] = groups < 65535u ? groups : 65535u;
}

// Imports what the pass giving an emitter's events their values over life
// binds beside the step's buffers.
static bool ImportEventValues(mvfxGpuSim* sim, Emitter* emitter)
{
    const mrhiBufferId own[4] = {emitter->eventNumbers, sim->ownCount, sim->ownSort,
                                 sim->ownDigits};
    bool imported = true;
    for (int32_t b = 0; imported && b < 4; ++b)
    {
        imported =
            mrhiImportBuffer(sim->device, own[b], &emitter->eventResources[b]) == mrhi_success;
    }
    return imported;
}

// Adds the pass giving an emitter's events their values over life, which
// binds the records kernel's buffers: its numbers, the particles (unread),
// the statistics, the grids, the events (as records), and the
// simulation's words for a count and a sort.
static bool AddEventValuesPass(mvfxGpuSim* sim, Emitter* emitter)
{
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId* o = emitter->eventResources;
    const mrhiAccess used[8] = {{.resource = o[0], .kind = mrhi_accessUniform},
                                {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageRead},
                                {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageRead},
                                {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessStorageRead},
                                {.resource = r[BUFFER_EVENTS], .kind = mrhi_accessStorageReadWrite},
                                {.resource = o[1], .kind = mrhi_accessStorageReadWrite},
                                {.resource = o[2], .kind = mrhi_accessStorageReadWrite},
                                {.resource = o[3], .kind = mrhi_accessStorageReadWrite}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passGraphics;
    def.accesses = used;
    def.accessCount = 8;
    def.neverCull = true;
    return mrhiAddPass(sim->device, &def, &emitter->passes[PASS_EVENT_VALUES]) == mrhi_success;
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
                                                emitter->blocks,
                                                emitter->grids,
                                                EventsOf(sim, emitter)};
    for (int32_t b = 0; b < BUFFER_COUNT; ++b)
    {
        if (mrhiImportBuffer(sim->device, buffers[b], &emitter->resources[b]) != mrhi_success)
        {
            return false;
        }
    }
    bool events = emitter->events.index1 != 0;
    if (events && !ImportEventValues(sim, emitter))
    {
        return false;
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written[5] = {
        {.resource = r[BUFFER_STEP], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_BIRTHS], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessCopyDestination},
        {.resource = emitter->eventResources[0], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[BUFFER_COUNT + 2] = {
        {.resource = r[BUFFER_STEP], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_BIRTHS], .kind = mrhi_accessStorageRead},
        {.resource = r[BUFFER_CURRENT], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_ACCEPTED], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_BLOCKS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessStorageRead},
        {.resource = r[BUFFER_EVENTS], .kind = mrhi_accessStorageReadWrite},
        {.resource = sim->bound[0], .kind = mrhi_accessSampled, .range = TEXTURE_RANGES[0]},
        {.resource = sim->bound[1], .kind = mrhi_accessSampled, .range = TEXTURE_RANGES[1]}};
    for (int32_t p = 0; p < (events ? PASS_EVENT_VALUES : PASS_EVENT_COUNT); ++p)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p == PASS_WRITE ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p == PASS_WRITE ? written : used;
        def.accessCount = p == PASS_WRITE ? (events ? 5 : 4) : BUFFER_COUNT + 2;
        // Its writes outlive the frame, read by the next step or the host.
        def.neverCull = true;
        if (mrhiAddPass(sim->device, &def, &emitter->passes[p]) != mrhi_success)
        {
            return false;
        }
    }
    return !events || AddEventValuesPass(sim, emitter);
}

mvfxResult mvfxGpuSim_HostGridBytes(const mvfxContext* context, const mvfxCollider* colliders,
                                    int32_t count, int64_t* bytesOut)
{
    if (bytesOut != nullptr)
    {
        *bytesOut = 0;
    }
    if (context == nullptr || bytesOut == nullptr || count < 0 || count > MVFX_MAX_COLLIDERS ||
        (colliders == nullptr && count > 0))
    {
        return mvfx_errorInvalid;
    }
    return mvfxHostGridBytes(context, colliders, count, bytesOut) ? mvfx_success : mvfx_errorStale;
}

mvfxResult mvfxGpuSim_SetDepthTextures(mvfxGpuSim* sim, mrhiResourceId depth,
                                       mrhiResourceId normals)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    sim->hostDepth = depth;
    sim->hostNormals = depth.index1 != 0 ? normals : (mrhiResourceId){0};
    return mvfx_success;
}

// Brings the depth and normal textures the step binds into the frame: the
// host's, or the simulation's own.
static bool BindTextures(mvfxGpuSim* sim)
{
    const mrhiResourceId host[2] = {sim->hostDepth, sim->hostNormals};
    const mrhiTextureId own[2] = {sim->ownDepth, sim->ownNormals};
    bool bound = true;
    for (int32_t t = 0; bound && t < 2; ++t)
    {
        sim->bound[t] = host[t];
        bound = host[t].index1 != 0 ||
                mrhiImportTexture(sim->device, own[t], &sim->bound[t]) == mrhi_success;
    }
    return bound;
}

mvfxResult mvfxGpuSim_AddPasses(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    // The instance's colliders change at any step: their grids go in the
    // room the def sets aside, every emitter's placed before a pass is
    // added, so a step refused adds none.
    mvfxEmitterPlan plan;
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        // A frame the host dropped leaves its passes behind: they go.
        emitter->added = false;
        emitter->records.count = 0;
        mvfxResult result = mvfxInstance_GetEmitterPlan(sim->context, sim->instance, e, &plan);
        if (result != mvfx_success)
        {
            return result;
        }
        if (emitter->capacity > 0 && plan.depth.enabled && sim->hostDepth.index1 == 0)
        {
            return mvfx_errorState;
        }
        if (emitter->capacity > 0 &&
            !mvfxPlaceHostGrids(sim->context, &plan, plan.ownColliderCount, &emitter->gridLayout))
        {
            return mvfx_errorCapacity;
        }
    }
    if (!BindTextures(sim))
    {
        return mvfx_errorState;
    }
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        mvfxResult result = mvfxInstance_GetEmitterPlan(sim->context, sim->instance, e, &plan);
        if (result != mvfx_success)
        {
            return result;
        }
        if (emitter->capacity == 0)
        {
            continue;
        }
        int32_t own = plan.ownColliderCount;
        uint32_t forceGrids[MVFX_MAX_FORCES][5];
        for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
        {
            mvfxForceGridWords(&emitter->gridLayout, f, forceGrids[f]);
        }
        uint32_t colliderGrids[2 * MVFX_MAX_COLLIDERS][5];
        for (int32_t c = 0; c < plan.colliderCount; ++c)
        {
            mvfxColliderGridWords(&emitter->gridLayout, &plan, own, c, colliderGrids[c]);
        }
        mvfxStepNumbers(&sim->views[e], &plan, e, (const uint32_t (*)[5])forceGrids,
                        (const uint32_t (*)[5])colliderGrids, sim->hostNormals.index1 != 0,
                        &emitter->tables, emitter->words);
        emitter->spans = mvfxStepSpansOf(&plan);
        mvfxRecordMap(&sim->views[e], &plan, emitter->map);
        EventNumbers(sim, e, &plan);
        emitter->plannedBirths = plan.births;
        emitter->birthCount = plan.birthCount;
        emitter->hostCount = plan.hostBirthCount;
        if (plan.hostBirthCount > 0)
        {
            mvfxHostBirthWords(&sim->views[e], &plan, emitter->hostWords);
        }
        emitter->cleared = plan.cleared;
        if (!AddEmitterPasses(sim, emitter))
        {
            return mvfx_errorState;
        }
        emitter->added = true;
    }
    return mvfx_success;
}

// Records the writes of the step's numbers its kernels read.
static bool WriteNumbers(mvfxGpuSim* sim, mrhiPassId pass, mrhiResourceId step, Emitter* emitter)
{
    bool written = true;
    for (int32_t s = 0; written && s < 4; ++s)
    {
        uint32_t first = emitter->spans.first[s];
        written =
            mrhiWriteBuffer(sim->device, pass, step, (uint64_t)first * 4u, emitter->words + first,
                            (uint64_t)emitter->spans.count[s] * 4u) == mrhi_success;
    }
    // The sources' block, unchanged from step to step, at the first.
    if (written && !emitter->sourcesWritten)
    {
        written = mrhiWriteBuffer(sim->device, pass, step, MVFX_SOURCES_FIRST * 4u,
                                  emitter->words + MVFX_SOURCES_FIRST,
                                  (MVFX_STEP_WORDS - MVFX_SOURCES_FIRST) * 4u) == mrhi_success;
        emitter->sourcesWritten = written;
    }
    return written;
}

// Records the pass writing the plan's bytes: its numbers, its births and
// its host births after the births' room, the record's map, the counts
// emptied after a clear, and the grids changed.
static bool RecordWrites(mvfxGpuSim* sim, Emitter* emitter)
{
    static const uint32_t empty[MVFX_RECORD_PAST + 1] = {0};
    mrhiPassId pass = emitter->passes[PASS_WRITE];
    const mrhiResourceId* r = emitter->resources;
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        WriteNumbers(sim, pass, r[BUFFER_STEP], emitter) &&
        (emitter->birthCount == 0 ||
         mrhiWriteBuffer(sim->device, pass, r[BUFFER_BIRTHS], 0, emitter->plannedBirths,
                         (uint64_t)emitter->birthCount * sizeof(mvfxPlannedBirth)) ==
             mrhi_success) &&
        (emitter->hostCount == 0 ||
         mrhiWriteBuffer(
             sim->device, pass, r[BUFFER_BIRTHS], BirthBytes(emitter), emitter->hostWords,
             (uint64_t)emitter->hostCount * MVFX_HOST_BIRTH_WORDS * 4u) == mrhi_success) &&
        mrhiWriteBuffer(sim->device, pass, r[BUFFER_STATISTICS],
                        MVFX_RECORD_SPACE * sizeof(uint32_t), emitter->map,
                        sizeof emitter->map) == mrhi_success &&
        (!emitter->cleared || mrhiWriteBuffer(sim->device, pass, r[BUFFER_STATISTICS], 0, empty,
                                              sizeof empty) == mrhi_success) &&
        mvfxWriteGrids(sim->device, pass, r[BUFFER_GRIDS], sim->context, &emitter->gridLayout) &&
        (emitter->events.index1 == 0 ||
         mrhiWriteBuffer(sim->device, pass, emitter->eventResources[0], 0, emitter->eventWords,
                         sizeof emitter->eventWords) == mrhi_success);
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records a kernel's pass over the emitter's buffers.
static bool RecordKernel(mvfxGpuSim* sim, const Emitter* emitter, int32_t p, mvfxKernel kernel,
                         uint32_t groups)
{
    mrhiPassId pass = emitter->passes[p];
    mrhiBinding bindings[BUFFER_COUNT + 2];
    for (int32_t b = 0; b < BUFFER_COUNT; ++b)
    {
        bindings[b] = (mrhiBinding){
            .slot = (uint32_t)b, .resource = emitter->resources[b], .size = MRHI_WHOLE_SIZE};
    }
    for (int32_t t = 0; t < 2; ++t)
    {
        bindings[BUFFER_COUNT + t] = (mrhiBinding){.slot = (uint32_t)(BUFFER_COUNT + t),
                                                   .resource = sim->bound[t],
                                                   .viewKind = mrhi_texture2d,
                                                   .range = TEXTURE_RANGES[t]};
    }
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, kernel)) ==
            mrhi_success &&
        mrhiSetBindings(sim->device, pass, 0, bindings, BUFFER_COUNT + 2) == mrhi_success &&
        mrhiDispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records the pass giving an emitter's events their values over life.
static bool RecordEventValues(mvfxGpuSim* sim, const Emitter* emitter)
{
    mrhiPassId pass = emitter->passes[PASS_EVENT_VALUES];
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId* o = emitter->eventResources;
    const mrhiResourceId bound[8] = {
        o[0], r[BUFFER_OTHER], r[BUFFER_STATISTICS], r[BUFFER_GRIDS], r[BUFFER_EVENTS], o[1], o[2],
        o[3]};
    mrhiBinding bindings[8];
    for (int32_t b = 0; b < 8; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mrhiSetComputePipeline(sim->device, pass,
                               mvfxGpuPipeline(sim->gpu, mvfx_kernelEventValues)) == mrhi_success &&
        mrhiSetBindings(sim->device, pass, 0, bindings, 8) == mrhi_success &&
        mrhiDispatch(sim->device, pass, emitter->eventWords[5], 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records an emitter's events kernels, when it writes events.
static bool RecordEvents(mvfxGpuSim* sim, const Emitter* emitter, uint32_t blocks)
{
    return emitter->events.index1 == 0 ||
           (RecordKernel(sim, emitter, PASS_EVENT_COUNT, mvfx_kernelEventCount, blocks) &&
            RecordKernel(sim, emitter, PASS_EVENT_OFFSETS, mvfx_kernelEventOffsets, 1) &&
            RecordKernel(sim, emitter, PASS_EVENT_WRITE, mvfx_kernelEventWrite, blocks) &&
            RecordEventValues(sim, emitter));
}

// Records an emitter's step passes, if added: the plan's writes and the
// kernels; its particles are in the other buffer after.
static bool RecordStep(mvfxGpuSim* sim, Emitter* emitter)
{
    if (!emitter->added)
    {
        return true;
    }
    emitter->added = false;
    uint32_t groups = mvfxUpdateGroups(emitter->capacity);
    uint32_t blocks = mvfxScanBlocks(emitter->capacity);
    bool recorded = RecordWrites(sim, emitter) &&
                    RecordKernel(sim, emitter, PASS_ROOM, mvfx_kernelRoom, 1) &&
                    RecordKernel(sim, emitter, PASS_UPDATE, mvfx_kernelUpdate, groups) &&
                    RecordKernel(sim, emitter, PASS_KILL, mvfx_kernelKillBirths, groups) &&
                    RecordKernel(sim, emitter, PASS_REDUCE, mvfx_kernelReduce, blocks) &&
                    RecordKernel(sim, emitter, PASS_OFFSETS, mvfx_kernelOffsets, 1) &&
                    RecordKernel(sim, emitter, PASS_SCATTER, mvfx_kernelScatter, blocks) &&
                    RecordKernel(sim, emitter, PASS_MEASURE, mvfx_kernelMeasure, 1) &&
                    RecordEvents(sim, emitter, blocks);
    emitter->current = recorded ? 1 - emitter->current : emitter->current;
    return recorded;
}

mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxRecordPipelines pipelines = {
        .records = mvfxGpuPipeline(sim->gpu, mvfx_kernelRecords),
        .keys = mvfxGpuPipeline(sim->gpu, mvfx_kernelKeys),
        .offsets = mvfxGpuPipeline(sim->gpu, mvfx_kernelSortOffsets),
        .scatter = mvfxGpuPipeline(sim->gpu, mvfx_kernelSortScatter)};
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        // The step's passes, then the record passes after them.
        if (!RecordStep(sim, emitter) ||
            !mvfxRecordRecordPasses(sim->device, &pipelines, &emitter->records))
        {
            return mvfx_errorState;
        }
    }
    // The host's textures were the frame's.
    sim->hostDepth = (mrhiResourceId){0};
    sim->hostNormals = (mrhiResourceId){0};
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
