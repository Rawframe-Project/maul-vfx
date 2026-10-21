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
#include "upload.h"

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
// field, gas) and of collider (plane, sphere, box, heightfield, distance grid).
#define RUN_FEATURES                                                                               \
    (MVFX_FEATURE_INHERIT_VELOCITY | MVFX_FEATURE_FORCES | MVFX_FEATURE_COLLISION |                \
     MVFX_FEATURE_SIZE | MVFX_FEATURE_COLOR | MVFX_FEATURE_CUSTOM | MVFX_FEATURE_FRAME |           \
     MVFX_FEATURE_MESH_INDEX | MVFX_FEATURE_OVER_LIFE | MVFX_FEATURE_ANGLE |                       \
     MVFX_FEATURE_ORIENTATION | MVFX_FEATURE_DRAG_SCALE | MVFX_FEATURE_VELOCITY_MODULES |          \
     MVFX_FEATURE_KILL_RULES | MVFX_FEATURE_RATE_CURVE | MVFX_FEATURE_DISTANCE |                   \
     MVFX_FEATURE_HOST_SPAWN | MVFX_FEATURE_SORT | MVFX_FEATURE_EVENTS |                           \
     MVFX_FEATURE_EVENT_SPAWN | MVFX_FEATURE_RIBBON | MVFX_FEATURE_BEAM | MVFX_FEATURE_LIGHT |     \
     MVFX_FEATURE_NEIGHBORS | MVFX_FEATURE_STEERING | MVFX_FEATURE_PAIR_FORCE |                    \
     MVFX_FEATURE_CONTACTS | MVFX_FEATURE_PLEXUS | MVFX_FEATURE_UNITS | MVFX_FEATURE_RIGID |       \
     MVFX_FEATURE_GAS_SPLAT | MVFX_FEATURE_LIQUID)
#define RUN_FORCES    0x7Fu
#define RUN_COLLIDERS 0x1Fu

// Whether an emitter's rigid bodies meet each other: they collide and
// keep body contacts.
static bool MeetsBodies(const mvfxEmitterView* view)
{
    return (view->features & MVFX_FEATURE_COLLISION) != 0 && view->rigid.bodyContacts > 0;
}

// The least power of two at least n.
static uint64_t PowerAbove(uint64_t n)
{
    uint64_t p = 1u;
    while (p < n)
    {
        p <<= 1u;
    }
    return p;
}

// The words an emitter's rigid pairs take in the blocks' buffer after the
// blocks' (sim/rigid_pairs.wgsl): its bodies' words and the grid's sorted
// entries, the pool's room, the runs' four arrays and their two sorted
// lists; none for an emitter whose bodies do not meet each other.
static uint64_t RigidPairWords(const mvfxEmitterView* view, int32_t capacity)
{
    if (!view->rigid.enabled || capacity <= 0 || !MeetsBodies(view))
    {
        return 0u;
    }
    uint64_t c = (uint64_t)capacity;
    uint64_t room = (uint64_t)view->rigid.bodyContacts * c;
    return MVFX_RIGID_BODY_WORDS * c + 2u * PowerAbove(c) + MVFX_RIGID_PAIR_WORDS * room +
           4u * room + 2u * PowerAbove(2u * room) + 2u * PowerAbove(room);
}

// Whether the part runs an emitter; what it does not, in refusal.
static bool Runs(const mvfxEmitterView* view, mvfxGpuRefusal* refusal)
{
    *refusal = (mvfxGpuRefusal){
        .gas = -1,
        .features =
            (view->features & ~RUN_FEATURES) |
            ((view->forceKinds & ~RUN_FORCES) != 0 ? MVFX_FEATURE_FORCES : 0) |
            ((view->colliderKinds & ~RUN_COLLIDERS) != 0 || view->sceneQueries
                 ? MVFX_FEATURE_COLLISION
                 : 0) |
            // Contacts see the particles a step kills, and move births
            // after ribbons and beams take their places.
            ((view->features & MVFX_FEATURE_CONTACTS) != 0 &&
                     (view->kills ||
                      (view->features & (MVFX_FEATURE_RIBBON | MVFX_FEATURE_BEAM)) != 0)
                 ? MVFX_FEATURE_CONTACTS
                 : 0) |
            // Rigid particles are killed by their strikes' life
            // alone.
            ((view->features & MVFX_FEATURE_RIGID) != 0 && view->kills ? MVFX_FEATURE_RIGID : 0),
    };
    return refusal->features == 0;
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
    *refusal = (mvfxGpuRefusal){.emitter = -1, .gas = -1};
    mvfxOrderSimEmitters(sim);
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

// The bytes of the host records the births buffer holds after the births,
// and the first byte of the plan's births staged after them for an
// emitter that spawns from events (its births are merged with the event
// births into the births' room).
static uint64_t HostRecordBytes(const Emitter* emitter)
{
    return (uint64_t)emitter->hostRecords * MVFX_HOST_BIRTH_WORDS * 4u;
}

static uint64_t StagedBirths(const Emitter* emitter)
{
    return BirthBytes(emitter) + HostRecordBytes(emitter);
}

// The words of an emitter's births' marks (two a particle slot), and for
// one whose births join ribbons the ribbons pass's words after them
// (rhi/kernels/sim/ribbons.wgsl): where each birth went, its two places
// (six words a birth), eight arrays over the appended, a word of head and
// a window of 256.
static uint64_t AcceptedWords(const Emitter* emitter)
{
    uint64_t c = (uint64_t)emitter->capacity;
    return emitter->ribbons ? 24u * c + 1u + 256u : 2u * c;
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
           CreateBuffer(sim, emitter->framesAt + mvfxSimFrameBytes(emitter), written,
                        &emitter->births) &&
           CreateBuffer(sim, MVFX_STEP_WORDS * 4u, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                        &emitter->step) &&
           CreateBuffer(sim, MVFX_GPU_STATISTICS_WORDS * 4u, written | mrhi_bufferCopySource,
                        &emitter->statistics) &&
           CreateBuffer(sim, AcceptedWords(emitter) * sizeof(uint32_t), mrhi_bufferStorage,
                        &emitter->accepted) &&
           CreateBuffer(sim,
                        (8u * mvfxScanBlocks(emitter->capacity) + emitter->rigidPairWords) *
                            sizeof(uint32_t),
                        mrhi_bufferStorage, &emitter->blocks) &&
           CreateBuffer(sim, 4u * (emitter->gridLayout.words > 0 ? emitter->gridLayout.words : 1u),
                        written, &emitter->grids);
}

// Creates an emitter's sort buffers when it sorts by place or is drawn
// as ribbons: keys and indices twice over its blocks (and for ribbons the
// chains' words after them), and two sets of digit counts. One that
// carries lights alone takes digit counts of two words a block (its
// lights' counts and offsets); one of sheets alone a sort buffer of two
// words a particle and one (ribbon_records/sheets.wgsl's quads' places
// and sheets' offsets, the sheets that fit).
static bool CreateSortBuffers(mvfxGpuSim* sim, const mvfxEmitterView* view, Emitter* emitter)
{
    bool ribbons = view->ribbon.ids != mvfx_ribbonNone;
    bool sheets = view->unitPoints > 1 && view->unit.kind == mvfx_unitSheet;
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    if (mvfxRecordOrder(view) != MVFX_ORDER_SORTED && !ribbons)
    {
        return (!view->light.enabled || CreateBuffer(sim, 2u * (blocks > 0 ? blocks : 1u) * 4u,
                                                     mrhi_bufferStorage, &emitter->digits)) &&
               (!sheets || CreateBuffer(sim, 4u * (2u * (uint64_t)emitter->capacity + 1u),
                                        mrhi_bufferStorage, &emitter->sort));
    }
    uint64_t words = ribbons ? MVFX_RIBBON_SORT_WORDS : 4u;
    return CreateBuffer(sim, 4u * words * blocks * MVFX_SORT_BLOCK, mrhi_bufferStorage,
                        &emitter->sort) &&
           CreateBuffer(sim, 2u * 256u * blocks * 4u, mrhi_bufferStorage, &emitter->digits);
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
static_assert(MVFX_GPU_EVENT_SERIALS == MVFX_RECORD_EVENT_SERIALS,
              "the event births' serials, as the record's");

// Allocates an emitter's staging of its host births and its host's
// frames, as many as it holds.
static mvfxResult AllocateStaging(mvfxGpuSim* sim, Emitter* emitter)
{
    if (emitter->hostCapacity > 0)
    {
        emitter->hostWords = mvfxRhiAllocate(&sim->allocator, HostBytes(emitter), 4);
    }
    if (emitter->frameCapacity > 0)
    {
        emitter->frameWords = mvfxRhiAllocate(&sim->allocator, mvfxSimFrameBytes(emitter), 4);
    }
    bool held = (emitter->hostCapacity == 0 || emitter->hostWords != nullptr) &&
                (emitter->frameCapacity == 0 || emitter->frameWords != nullptr);
    return held ? mvfx_success : mvfx_errorCapacity;
}

// Sizes an emitter's memory by its plan and makes it: the host's
// particles' words, the grids and tables laid out, the buffers.
static mvfxResult PrepareEmitter(mvfxGpuSim* sim, const mvfxGpuSimDef* def, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    mvfxEmitterPlan plan;
    mvfxResult result = mvfxInstance_GetEmitterStepPlan(def->context, def->instance, e, 0, &plan);
    emitter->capacity = plan.capacity;
    emitter->hostCapacity = plan.capacity > 0 ? sim->views[e].hostSpawnCapacity : 0;
    const mvfxEventSpawn* spawn = &sim->views[e].eventSpawn;
    emitter->spawnSource = spawn->enabled ? spawn->source : -1;
    // Event births past the births' room are never written.
    int32_t room = 2 * plan.capacity;
    emitter->hostRecords = emitter->hostCapacity +
                           (spawn->enabled ? (spawn->capacity < room ? spawn->capacity : room) : 0);
    // The host's frames after the births, the host records and a spawning
    // emitter's staged births.
    emitter->framesAt = StagedBirths(emitter) + (spawn->enabled ? BirthBytes(emitter) : 0);
    emitter->particleWords = sim->views[e].arrays.count + MVFX_GPU_OWN_WORDS;
    emitter->ribbons = sim->views[e].ribbon.ids != mvfx_ribbonNone;
    emitter->tears = sim->views[e].unitPoints > 1 && sim->views[e].unit.tearStrain > 0.0f;
    emitter->beam = sim->views[e].beam.enabled;
    // A unit a workgroup of the unit pass, at most a dispatch's.
    int32_t points = sim->views[e].unitPoints;
    emitter->units = points > 1 && plan.capacity > 0;
    emitter->rigid = sim->views[e].rigid.enabled && plan.capacity > 0;
    emitter->rigidPairWords = RigidPairWords(&sim->views[e], plan.capacity);
    emitter->rigidPairs = emitter->rigidPairWords > 0u;
    int32_t units = points > 1 ? plan.capacity / points : 0;
    emitter->unitGroups = (uint32_t)(units < 65535 ? (units > 0 ? units : 1) : 65535);
    emitter->pinsHead = sim->views[e].ribbon.pinHead;
    emitter->gridMask = plan.gridMask;
    result = result == mvfx_success ? mvfxSizeSimFrames(sim, def, e, plan.capacity) : result;
    result = result == mvfx_success ? AllocateStaging(sim, emitter) : result;
    result = result == mvfx_success ? mvfxHoldSimHullFaces(sim, &sim->views[e], emitter) : result;
    uint32_t hostWords = (uint32_t)((def->hostGridBytes + 3) / 4);
    if (result == mvfx_success &&
        !mvfxLayGrids(def->context, sim->views[e].forceGrids, sim->views[e].colliderGrids,
                      hostWords, &emitter->gridLayout))
    {
        result = mvfx_errorStale;
    }
    if (result == mvfx_success)
    {
        mvfxLaySimTables(&sim->views[e], emitter);
        mvfxLaySimFollow(sim, e);
    }
    if (result == mvfx_success && emitter->capacity > 0 &&
        sim->views[e].shape.kind == mvfx_shapeMesh)
    {
        result = mvfxLaySimMesh(sim, def, e);
    }
    if (result == mvfx_success && emitter->capacity > 0 &&
        (!CreateBuffers(sim, emitter) || !mvfxCreateRecordBuffers(sim->device, &emitter->records) ||
         !CreateSortBuffers(sim, &sim->views[e], emitter) || !mvfxCreateSimEvents(sim, e) ||
         !mvfxCreateSimGrid(sim, e) || !mvfxCreateSimContacts(sim, e) ||
         !mvfxCreateSimPlexus(sim, e) || !mvfxCreateSimSelf(sim, e) ||
         !mvfxCreateSimLiquid(sim, e)))
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
    mvfxGpuRefusal refusal = {.emitter = -1, .gas = -1};
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
    mvfxResult planned = mvfxInstance_GetEmitterStepPlan(def->context, def->instance, 0, 0, &plan);
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
    if (result == mvfx_success)
    {
        result = mvfxReadSimGases(sim, def->effect, &refusal.gas);
    }
    for (int32_t e = 0; result == mvfx_success && e < sim->emitterCount; ++e)
    {
        result = PrepareEmitter(sim, def, e);
    }
    for (int32_t g = 0; result == mvfx_success && g < sim->gasCount; ++g)
    {
        result = mvfxCreateSimGas(sim, &sim->gases[g], (uint32_t)((def->hostGridBytes + 3) / 4))
                     ? mvfx_success
                     : mvfx_errorState;
    }
    if (result == mvfx_success && !mvfxCreateSimLanes(sim))
    {
        result = mvfx_errorCapacity;
    }
    mvfxSrgbSteps(sim->srgb);
    if (result == mvfx_success &&
        (!CreateTextures(sim) ||
         !CreateBuffer(sim, 4u * MVFX_GPU_RIBBON_COUNTS, mrhi_bufferStorage, &sim->ownCount) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownSort) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownDigits) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownEvents) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownGrid[0]) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownGrid[1]) ||
         !CreateBuffer(sim, 4, mrhi_bufferStorage, &sim->ownGrid[2])))
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
    if (sim == nullptr || sim->ending)
    {
        return;
    }
    sim->ending = true;
    mvfxDestroySimLanes(sim);
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
        mvfxDestroySimGrid(sim, emitter);
        mvfxDestroySimContacts(sim, emitter);
        mvfxDestroySimPlexus(sim, emitter);
        mvfxDestroySimSelf(sim, emitter);
        mvfxDestroySimLiquid(sim, emitter);
        mvfxRhiRelease(&sim->allocator, emitter->hostWords, HostBytes(emitter), 4);
        mvfxRhiRelease(&sim->allocator, emitter->frameWords, mvfxSimFrameBytes(emitter), 4);
        mvfxRhiRelease(&sim->allocator, emitter->meshImage, emitter->meshBytes, 4);
        mvfxRhiRelease(&sim->allocator, emitter->hullFaces, 4u * emitter->hullFaceWords, 4);
    }
    for (int32_t g = 0; g < sim->gasCount; ++g)
    {
        mvfxDestroySimGas(sim, &sim->gases[g]);
    }
    DestroyBuffer(sim, sim->ownCount);
    DestroyBuffer(sim, sim->ownSort);
    DestroyBuffer(sim, sim->ownDigits);
    DestroyBuffer(sim, sim->ownEvents);
    for (int32_t g = 0; g < 3; ++g)
    {
        DestroyBuffer(sim, sim->ownGrid[g]);
    }
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
        .events = e->writesEvents ? e->events : (mrhiBufferId){0},
        .eventFirst = e->writesEvents ? mvfxEventFirst(e->capacity) : 0,
        .eventCapacity = e->writesEvents ? sim->views[emitter].eventCapacity : 0,
        .links = e->plexus ? e->plexusBuffers[PLEXUS_LINKS] : (mrhiBufferId){0},
        .linkCapacity = e->plexus ? sim->views[emitter].plexus.maxLinks : 0,
    };
    return mvfx_success;
}

// An emitter's events buffer, or the simulation's word for one that
// writes none.
static mrhiBufferId EventsOf(const mvfxGpuSim* sim, const Emitter* emitter)
{
    return emitter->events.index1 != 0 ? emitter->events : sim->ownEvents;
}

// The passes an emitter's step adds, in order: the plan's bytes, its
// births from events when it spawns from them, its kernels, its events'
// when it writes any.
static int32_t PassOrder(const Emitter* emitter, int32_t order[PASS_COUNT])
{
    int32_t count = 0;
    order[count++] = PASS_WRITE;
    for (int32_t p = PASS_SPAWN_COPY; emitter->spawnSource >= 0 && p <= PASS_SPAWN_MERGE; ++p)
    {
        order[count++] = p;
    }
    for (int32_t p = PASS_ROOM; p < (emitter->writesEvents ? PASS_EVENT_VALUES : PASS_EVENT_COUNT);
         ++p)
    {
        if ((p != PASS_RIBBONS || emitter->ribbons) && (p != PASS_BEAM || emitter->beam) &&
            (p != PASS_REBOX || emitter->contacts) &&
            (p != PASS_UNITS || (emitter->units && !emitter->selfCollides) || emitter->rigid))
        {
            order[count++] = p;
        }
    }
    return count;
}

// Adds the passes an emitter's pass p is followed by: the neighbor grid's
// after room's, which tells them whether the births fit; units'
// self-collision's in place of the unit pass, after the kills', the unit
// pass's halves with the step's accesses (used); the particle contacts'
// after the scatter, on the pool it leaves; the plexus links' after the
// bounds, of the places at the step's end.
static bool AddPassesAfter(mvfxGpuSim* sim, Emitter* emitter, int32_t p, const mrhiAccess* used)
{
    return (p != PASS_ROOM || mvfxAddSimGridPasses(sim, emitter)) &&
           (p != PASS_KILL || !emitter->liquid ||
            mvfxAddSimLiquidPasses(sim, emitter, used, BUFFER_COUNT + 2)) &&
           (p != PASS_KILL || !emitter->selfCollides ||
            mvfxAddSimSelfPasses(sim, emitter, used, BUFFER_COUNT + 2)) &&
           (p != PASS_SCATTER || mvfxAddSimContactPasses(sim, emitter)) &&
           (p != PASS_MEASURE || mvfxAddSimPlexusPasses(sim, emitter));
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
    bool events = emitter->writesEvents;
    if (events && !mvfxImportEventValues(sim, emitter))
    {
        return false;
    }
    const mrhiResourceId* r = emitter->resources;
    mrhiAccess written[5 + MVFX_MAX_GASES] = {
        {.resource = r[BUFFER_STEP], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_BIRTHS], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessCopyDestination},
        {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessCopyDestination},
        {.resource = emitter->eventResources[0], .kind = mrhi_accessCopyDestination}};
    int32_t writes = events ? 5 : 4;
    if (!mvfxImportSimFollow(sim, emitter, written, &writes))
    {
        return false;
    }
    const mrhiAccess used[BUFFER_COUNT + 2] = {
        {.resource = r[BUFFER_STEP], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_BIRTHS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_CURRENT], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_ACCEPTED], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_BLOCKS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessStorageRead},
        {.resource = r[BUFFER_EVENTS], .kind = mrhi_accessStorageReadWrite},
        {.resource = sim->bound[0], .kind = mrhi_accessSampled, .range = TEXTURE_RANGES[0]},
        {.resource = sim->bound[1], .kind = mrhi_accessSampled, .range = TEXTURE_RANGES[1]}};
    int32_t order[PASS_COUNT];
    int32_t count = PassOrder(emitter, order);
    for (int32_t n = 0; n < count; ++n)
    {
        int32_t p = order[n];
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = p == PASS_WRITE ? mrhi_passTransfer : mrhi_passGraphics;
        def.accesses = p == PASS_WRITE ? written : used;
        def.accessCount = p == PASS_WRITE ? writes : BUFFER_COUNT + 2;
        // Its writes outlive the frame, read by the next step or the host.
        def.neverCull = true;
        bool added = p == PASS_SPAWN_COPY
                         ? mvfxAddSpawnCopyPass(sim, emitter)
                         : mrhiAddPass(sim->device, &def, &emitter->passes[p]) == mrhi_success;
        added = added && AddPassesAfter(sim, emitter, p, used);
        if (!added)
        {
            return false;
        }
    }
    return !events || mvfxAddEventValuesPass(sim, emitter);
}

// Writes an emitter's numbers for its plan's step, with each grid where its
// layout has it.
static void StepNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    uint32_t forceGrids[MVFX_MAX_FORCES][5];
    for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
    {
        mvfxForceGridWords(&emitter->gridLayout, f, forceGrids[f]);
    }
    mvfxSimFollowGrids(sim, e, forceGrids);
    uint32_t colliderGrids[2 * MVFX_MAX_COLLIDERS][5];
    for (int32_t c = 0; c < plan->colliderCount; ++c)
    {
        mvfxColliderGridWords(&emitter->gridLayout, plan->colliders, plan->ownColliderCount, c,
                              colliderGrids[c]);
    }
    mvfxStepNumbers(&sim->views[e], plan, e, (const uint32_t (*)[5])forceGrids,
                    (const uint32_t (*)[5])colliderGrids, sim->hostNormals.index1 != 0,
                    &emitter->tables, emitter->words);
}

// Adds an emitter's step: its numbers, map and births from its plan, and
// its passes.
static mvfxResult AddStep(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    mvfxEmitterPlan plan;
    mvfxResult result = mvfxSimEmitterPlan(sim, e, &plan);
    if (result != mvfx_success || emitter->capacity == 0)
    {
        return result;
    }
    StepNumbers(sim, e, &plan);
    emitter->words[MVFX_UNIT_TEARS + 1] = emitter->tearFacts;
    result = mvfxSpawnStepNumbers(sim, e, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    emitter->spans = mvfxStepSpansOf(&plan, emitter->beam);
    mvfxRecordMap(&sim->views[e], &plan, emitter->map);
    mvfxRecordHead(&plan, emitter->head);
    mvfxSimEventNumbers(sim, e, &plan);
    mvfxSimGridNumbers(sim, e, &plan);
    mvfxSimContactNumbers(sim, e, &plan);
    mvfxSimSelfNumbers(sim, e, &plan);
    mvfxSimLiquidNumbers(sim, e, &plan);
    mvfxSimPlexusNumbers(sim, e, &plan);
    emitter->plannedBirths = plan.births;
    emitter->birthCount = plan.birthCount;
    emitter->hostCount = plan.hostBirthCount;
    if (plan.hostBirthCount > 0)
    {
        mvfxHostBirthWords(&sim->views[e], &plan, emitter->hostWords);
    }
    mvfxSimFrameNumbers(sim, emitter, &plan);
    emitter->cleared = plan.cleared;
    mvfxSimFollowSources(sim, emitter);
    if (!AddEmitterPasses(sim, emitter))
    {
        return mvfx_errorState;
    }
    emitter->added = true;
    return mvfx_success;
}

mvfxResult mvfxGpuSim_AddPasses(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    // The plans' steps, each a lane of every emitter and gas when there
    // are more (sim_lanes.c).
    mvfxBeginSimSteps(sim);
    sim->step = 0;
    sim->steps = 0;
    int32_t steps = 0;
    mvfxResult result = mvfxInstance_GetPlanSteps(sim->context, sim->instance, &steps);
    result = result == mvfx_success ? mvfxPrepareSimFrame(sim) : result;
    for (int32_t s = 0; result == mvfx_success && s < steps; ++s)
    {
        if (s > 0 && !mvfxEnterSimStep(sim, s))
        {
            return mvfx_errorState;
        }
        sim->step = s;
        // In step order: an emitter's source's passes before its own; the
        // gases after them all.
        for (int32_t n = 0; result == mvfx_success && n < sim->emitterCount; ++n)
        {
            result = AddStep(sim, sim->order[n]);
        }
        for (int32_t g = 0; result == mvfx_success && g < sim->gasCount; ++g)
        {
            result = mvfxAddSimGasPasses(sim, g);
        }
    }
    sim->steps = result == mvfx_success ? steps : 0;
    return result;
}

// Records the writes of the step's numbers its kernels read.
static bool WriteNumbers(mvfxGpuSim* sim, mrhiPassId pass, mrhiResourceId step, Emitter* emitter)
{
    bool written = true;
    for (int32_t s = 0; written && s < 4; ++s)
    {
        uint32_t first = emitter->spans.first[s];
        written = mvfxUpload(sim->device, &sim->uploaded, pass, step, (uint64_t)first * 4u,
                             emitter->words + first, (uint64_t)emitter->spans.count[s] * 4u);
    }
    // The sources' block, unchanged from step to step, at the first.
    if (written && !emitter->sourcesWritten)
    {
        written = mvfxUpload(sim->device, &sim->uploaded, pass, step, MVFX_SOURCES_FIRST * 4u,
                             emitter->words + MVFX_SOURCES_FIRST,
                             (MVFX_STEP_WORDS - MVFX_SOURCES_FIRST) * 4u);
        emitter->sourcesWritten = written;
    }
    // The units' frames, which the host's frames change from step to step.
    if (written && emitter->frameCapacity > 0)
    {
        written = mvfxUpload(sim->device, &sim->uploaded, pass, step, MVFX_UNIT_FRAMES * 4u,
                             emitter->words + MVFX_UNIT_FRAMES, 4u * 4u);
    }
    return written;
}

// Records the pass writing the plan's bytes: its numbers, its births and
// its host births after the births' room, the record's map, the counts
// emptied after a clear, the grids changed and the gases' faces.
static bool RecordWrites(mvfxGpuSim* sim, Emitter* emitter)
{
    mrhiPassId pass = emitter->passes[PASS_WRITE];
    const mrhiResourceId* r = emitter->resources;
    bool spawns = emitter->spawnSource >= 0;
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        WriteNumbers(sim, pass, r[BUFFER_STEP], emitter) &&
        (emitter->birthCount == 0 ||
         mvfxUpload(sim->device, &sim->uploaded, pass, r[BUFFER_BIRTHS],
                    spawns ? StagedBirths(emitter) : 0, emitter->plannedBirths,
                    (uint64_t)emitter->birthCount * sizeof(mvfxPlannedBirth))) &&
        (emitter->hostCount == 0 ||
         mvfxUpload(sim->device, &sim->uploaded, pass, r[BUFFER_BIRTHS], BirthBytes(emitter),
                    emitter->hostWords,
                    (uint64_t)emitter->hostCount * MVFX_HOST_BIRTH_WORDS * 4u)) &&
        mvfxUploadSimFrames(sim, emitter, pass) &&
        mvfxUpload(sim->device, &sim->uploaded, pass, r[BUFFER_STATISTICS],
                   MVFX_RECORD_SPACE * sizeof(uint32_t), emitter->map, sizeof emitter->map) &&
        (!emitter->pinsHead ||
         mvfxUpload(sim->device, &sim->uploaded, pass, r[BUFFER_STATISTICS],
                    MVFX_RECORD_HEAD * sizeof(uint32_t), emitter->head, sizeof emitter->head)) &&
        mvfxClearSimCounts(sim, pass, emitter) &&
        mvfxWriteGrids(sim->device, &sim->uploaded, pass, r[BUFFER_GRIDS], sim->context,
                       &emitter->gridLayout) &&
        mvfxWriteSimMesh(sim, pass, emitter) && mvfxCopySimFollow(sim, pass, emitter) &&
        (!emitter->writesEvents || mvfxWriteEventNumbers(sim, pass, emitter));
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records a kernel's pass over the emitter's buffers.
bool mvfxRecordSimKernel(mvfxGpuSim* sim, const Emitter* emitter, int32_t p, mvfxKernel kernel,
                         uint32_t groups)
{
    static const uint32_t none[4] = {0};
    return mvfxRecordSimDispatch(sim, emitter, emitter->passes[p], kernel, groups, none);
}

bool mvfxRecordSimDispatch(mvfxGpuSim* sim, const Emitter* emitter, mrhiPassId pass,
                           mvfxKernel kernel, uint32_t groups, const uint32_t root[4])
{
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
        mrhiSetRootBlock(sim->device, pass, 0, root, 4 * sizeof(uint32_t)) == mrhi_success &&
        mrhiDispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
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
    bool recorded =
        RecordWrites(sim, emitter) && mvfxRecordSpawn(sim, emitter, groups) &&
        mvfxRecordSimKernel(sim, emitter, PASS_ROOM, mvfx_kernelRoom, 1) &&
        mvfxRecordSimGrid(sim, emitter) &&
        mvfxRecordSimKernel(sim, emitter, PASS_UPDATE, mvfx_kernelUpdate, groups) &&
        mvfxRecordSimKernel(sim, emitter, PASS_KILL, mvfx_kernelKillBirths, groups) &&
        (!emitter->rigid ||
         (emitter->rigidPairs
              ? mvfxRecordSimKernel(sim, emitter, PASS_UNITS, mvfx_kernelRigidPairs, 1)
              : mvfxRecordSimKernel(sim, emitter, PASS_UNITS, mvfx_kernelRigid, groups))) &&
        (!emitter->liquid || mvfxRecordSimLiquid(sim, emitter)) &&
        (!emitter->units ||
         (emitter->selfCollides ? mvfxRecordSimSelf(sim, emitter)
                                : mvfxRecordSimKernel(sim, emitter, PASS_UNITS, mvfx_kernelUnits,
                                                      emitter->unitGroups))) &&
        mvfxRecordSimKernel(sim, emitter, PASS_REDUCE, mvfx_kernelReduce, blocks) &&
        mvfxRecordSimKernel(sim, emitter, PASS_OFFSETS, mvfx_kernelOffsets, 1) &&
        mvfxRecordSimKernel(sim, emitter, PASS_SCATTER, mvfx_kernelScatter, blocks) &&
        mvfxRecordSimContacts(sim, emitter) &&
        (!emitter->contacts ||
         mvfxRecordSimKernel(sim, emitter, PASS_REBOX, mvfx_kernelRebox, blocks)) &&
        (!emitter->ribbons ||
         mvfxRecordSimKernel(sim, emitter, PASS_RIBBONS, mvfx_kernelRibbons, 1)) &&
        (!emitter->beam || mvfxRecordSimKernel(sim, emitter, PASS_BEAM, mvfx_kernelBeam, 1)) &&
        mvfxRecordSimKernel(sim, emitter, PASS_MEASURE, mvfx_kernelMeasure, 1) &&
        mvfxRecordSimPlexus(sim, emitter) && mvfxRecordSimEvents(sim, emitter, blocks);
    emitter->current = recorded ? 1 - emitter->current : emitter->current;
    return recorded;
}

// Records a step's emitters' passes, then its gases'.
static bool RecordSteps(mvfxGpuSim* sim)
{
    bool recorded = true;
    for (int32_t n = 0; recorded && n < sim->emitterCount; ++n)
    {
        recorded = RecordStep(sim, &sim->emitters[sim->order[n]]);
    }
    for (int32_t g = 0; recorded && g < sim->gasCount; ++g)
    {
        recorded = mvfxRecordSimGas(sim, &sim->gases[g]);
    }
    return recorded;
}

mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    sim->uploaded = 0;
    const mvfxRecordPipelines pipelines = {
        .records = mvfxGpuPipeline(sim->gpu, mvfx_kernelRecords),
        .keys = mvfxGpuPipeline(sim->gpu, mvfx_kernelKeys),
        .offsets = mvfxGpuPipeline(sim->gpu, mvfx_kernelSortOffsets),
        .scatter = mvfxGpuPipeline(sim->gpu, mvfx_kernelSortScatter),
        .ribbonKeys = mvfxGpuPipeline(sim->gpu, mvfx_kernelRibbonKeys),
        .ribbonChains = mvfxGpuPipeline(sim->gpu, mvfx_kernelRibbonChains),
        .ribbonPoints = mvfxGpuPipeline(sim->gpu, mvfx_kernelRibbonPoints),
        .ribbonPieces = mvfxGpuPipeline(sim->gpu, mvfx_kernelRibbonPieces),
        .lightCount = mvfxGpuPipeline(sim->gpu, mvfx_kernelLightCount),
        .lightOffsets = mvfxGpuPipeline(sim->gpu, mvfx_kernelLightOffsets),
        .lightWrite = mvfxGpuPipeline(sim->gpu, mvfx_kernelLightWrite),
        .spriteQuads = mvfxGpuPipeline(sim->gpu, mvfx_kernelSpriteQuads),
        .sheetFit = mvfxGpuPipeline(sim->gpu, mvfx_kernelSheetFit),
        .sheetPoints = mvfxGpuPipeline(sim->gpu, mvfx_kernelSheetPoints)};
    // Each step's emitters and gases, the last step's record passes after
    // them; a split frame's steps recalled in turn (sim_lanes.c).
    bool split = sim->steps > 1;
    if (split)
    {
        mvfxStoreSimSteps(sim, sim->steps);
    }
    bool recorded = true;
    int32_t added = sim->step;
    for (int32_t s = 0; recorded && s < (split ? sim->steps : 1); ++s)
    {
        if (split)
        {
            mvfxRecallSimStep(sim, s);
        }
        sim->step = s;
        recorded = RecordSteps(sim);
    }
    sim->step = added;
    for (int32_t n = 0; recorded && n < sim->emitterCount; ++n)
    {
        recorded = mvfxRecordRecordPasses(sim->device, &sim->uploaded, &pipelines,
                                          &sim->emitters[sim->order[n]].records);
    }
    mvfxEndSimSteps(sim);
    sim->steps = 0;
    if (!recorded)
    {
        return mvfx_errorState;
    }
    // The host's textures were the frame's.
    sim->hostDepth = (mrhiResourceId){0};
    sim->hostNormals = (mrhiResourceId){0};
    return mvfx_success;
}

mvfxResult mvfxGpuSim_UploadBytes(const mvfxGpuSim* sim, int64_t* bytesOut)
{
    if (bytesOut != nullptr)
    {
        *bytesOut = 0;
    }
    if (sim == nullptr || bytesOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    *bytesOut = (int64_t)sim->uploaded;
    return mvfx_success;
}
