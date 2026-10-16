// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An instance's block: the header, the emitters' states, then each
// emitter's particle arrays, aligned for whole-width vector loops.

#include "instance.h"

#include "allocator.h"
#include "collide.h"
#include "force.h"
#include "gas_pressure.h"
#include "kill.h"

#include <math.h>
#include <string.h>

// Where an emitter's host queue and resolved forces, kill rules and
// colliders lie in the block.
typedef struct EmitterAt
{
    size_t queue;
    size_t forces;
    size_t rules;
    size_t colliders;
    size_t queries;
    size_t events;
    size_t eventQueue;
    size_t gridTable;
    size_t gridKeys;
    size_t gridIndex;
    size_t gridValues;
    uint32_t gridMask;
    size_t contactTable;
    size_t contactKeys;
    size_t contactIndex;
    size_t contactPlaces;
    uint32_t contactMask;
    size_t plexusTable;
    size_t plexusKeys;
    size_t plexusIndex;
    size_t plexusLinks;
    uint32_t plexusMask;
    size_t unitPrevious;
    size_t attachFrames;
    size_t attachTargets;
    size_t selfTable;
    size_t selfKeys;
    size_t selfIndex;
    uint32_t selfMask;
    size_t selfStart;
    size_t selfPairs;
    size_t selfCounts;
    size_t rigidStart;
    size_t rigidMass;
    size_t rigidFlags;
    size_t rigidIslands;
    size_t rigidCandidates;
    size_t rigidContacts;
    size_t rigidCounts;
    size_t rigidTable;
    size_t rigidKeys;
    size_t rigidIndex;
    uint32_t rigidMask;
    size_t rigidPairs;
    size_t liquidTable;
    size_t liquidKeys;
    size_t liquidIndex;
    size_t liquidArrays;
    uint32_t liquidMask;
} EmitterAt;

// The entries of a grid's table at a capacity: a power of two at least
// twice it.
static uint32_t GridTableSize(int32_t capacity)
{
    uint32_t size = 2;
    while (size < 2u * (uint32_t)capacity)
    {
        size *= 2u;
    }
    return size;
}

// Adds an emitter of units' room: the places the integrator predicts, and
// for units held to frames the host's frames and each unit's frame at the
// step's end.
static void AddUnits(mvfxLayout* layout, const mvfxEffect* effect, int32_t e,
                     const int32_t* capacities, EmitterAt* at)
{
    const mvfxCompiledEmitter* compiled = &effect->emitters[e];
    size_t previous = compiled->unitPoints > 1 ? 3u * (size_t)capacities[e] : 0;
    at->unitPrevious = mvfxLayoutAdd(layout, previous, sizeof(float), alignof(float));
    mvfxAttachKind kind = compiled->unitPoints > 1 ? compiled->unit.attach : mvfx_attachNone;
    size_t frames = kind == mvfx_attachHost ? (size_t)compiled->unit.attachFrameCapacity : 0;
    at->attachFrames =
        mvfxLayoutAdd(layout, frames, sizeof(mvfxAttachFrame), alignof(mvfxAttachFrame));
    bool framed = kind == mvfx_attachHost || kind == mvfx_attachParticles;
    size_t units = framed ? (size_t)(capacities[e] / compiled->unitPoints) : 0;
    at->attachTargets = mvfxLayoutAdd(layout, 7u * units, sizeof(float), alignof(float));
    bool self = compiled->unitPoints > 1 && compiled->unit.selfThickness > 0.0f;
    size_t capacity = self ? (size_t)capacities[e] : 0;
    uint32_t size = self ? GridTableSize(capacities[e]) : 0u;
    at->selfMask = self ? size - 1u : 0u;
    at->selfTable =
        mvfxLayoutAdd(layout, self ? (size_t)size + 1u : 0u, sizeof(int32_t), alignof(int32_t));
    at->selfKeys = mvfxLayoutAdd(layout, capacity, sizeof(uint32_t), alignof(uint32_t));
    at->selfIndex = mvfxLayoutAdd(layout, capacity, sizeof(int32_t), alignof(int32_t));
    at->selfStart = mvfxLayoutAdd(layout, 3u * capacity, sizeof(float), alignof(float));
    size_t most = self ? (size_t)compiled->unit.selfContacts : 0u;
    at->selfPairs = mvfxLayoutAdd(layout, most * capacity, sizeof(int32_t), alignof(int32_t));
    at->selfCounts = mvfxLayoutAdd(layout, capacity, sizeof(int32_t), alignof(int32_t));
}

// Adds an emitter of rigid particles' room: start poses, mass, flags,
// islands for sleeping, contacts with the world, and for bodies meeting
// each other their grid and pool.
static void AddRigid(mvfxLayout* layout, const mvfxCompiledEmitter* compiled, int32_t capacity,
                     EmitterAt* at)
{
    size_t rigid = compiled->rigid.enabled ? (size_t)capacity : 0u;
    size_t met = compiled->collision.enabled ? rigid : 0u;
    at->rigidStart = mvfxLayoutAdd(layout, 7u * rigid, sizeof(float), alignof(float));
    at->rigidMass = mvfxLayoutAdd(layout, (size_t)MVFX_RIGID_MASS_ARRAYS * rigid, sizeof(float),
                                  alignof(float));
    at->rigidFlags = mvfxLayoutAdd(layout, rigid, sizeof(uint8_t), alignof(uint8_t));
    size_t sleeping = compiled->rigid.sleepSpeed > 0.0f ? rigid : 0u;
    at->rigidIslands = mvfxLayoutAdd(layout, sleeping, sizeof(int32_t), alignof(int32_t));
    at->rigidContacts = mvfxLayoutAdd(layout, (size_t)MVFX_MAX_RIGID_CONTACTS * met,
                                      sizeof(mvfxBodyContact), alignof(mvfxBodyContact));
    at->rigidCounts = mvfxLayoutAdd(layout, met, sizeof(int32_t), alignof(int32_t));
    size_t pairs = met > 0u ? (size_t)compiled->rigid.bodyContacts * met : 0u;
    size_t grid = pairs > 0u ? met : 0u;
    uint32_t size = grid > 0u ? GridTableSize(capacity) : 0u;
    at->rigidMask = grid > 0u ? size - 1u : 0u;
    at->rigidTable = mvfxLayoutAdd(layout, grid > 0u ? (size_t)size + 1u : 0u, sizeof(int32_t),
                                   alignof(int32_t));
    at->rigidKeys = mvfxLayoutAdd(layout, grid, sizeof(uint32_t), alignof(uint32_t));
    at->rigidIndex = mvfxLayoutAdd(layout, grid, sizeof(int32_t), alignof(int32_t));
    at->rigidCandidates = mvfxLayoutAdd(layout, grid, sizeof(int32_t), alignof(int32_t));
    at->rigidPairs =
        mvfxLayoutAdd(layout, pairs, sizeof(mvfxPairContact), alignof(mvfxPairContact));
}

// Adds an emitter's liquid room to the block, for one whose particles are
// a liquid: its grid and thirteen arrays of the capacity.
static void AddLiquid(mvfxLayout* layout, const mvfxCompiledEmitter* compiled, int32_t capacity,
                      EmitterAt* at)
{
    if (!compiled->liquid.enabled)
    {
        return;
    }
    uint32_t size = GridTableSize(capacity);
    at->liquidMask = size - 1u;
    at->liquidTable = mvfxLayoutAdd(layout, (size_t)size + 1u, sizeof(int32_t), alignof(int32_t));
    at->liquidKeys = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(uint32_t), alignof(uint32_t));
    at->liquidIndex = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(int32_t), alignof(int32_t));
    at->liquidArrays = mvfxLayoutAdd(layout, 13u * (size_t)capacity, sizeof(float), alignof(float));
}

// Adds an emitter's neighbor grid to the block, for one queried.
static void AddGrid(mvfxLayout* layout, const mvfxCompiledEmitter* compiled, int32_t capacity,
                    EmitterAt* at)
{
    if (!(compiled->gridCell > 0.0f))
    {
        return;
    }
    uint32_t size = GridTableSize(capacity);
    at->gridMask = size - 1u;
    at->gridTable = mvfxLayoutAdd(layout, (size_t)size + 1u, sizeof(int32_t), alignof(int32_t));
    at->gridKeys = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(uint32_t), alignof(uint32_t));
    at->gridIndex = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(int32_t), alignof(int32_t));
    at->gridValues = mvfxLayoutAdd(layout, 6u * (size_t)capacity, sizeof(float), alignof(float));
}

static EmitterAt AddEmitter(mvfxLayout* layout, const mvfxCompiledEmitter* compiled)
{
    int32_t colliders = compiled->colliderCount;
    if (compiled->collision.enabled && compiled->collision.hostColliders)
    {
        colliders += MVFX_MAX_COLLIDERS;
    }
    bool scene = compiled->collision.enabled && compiled->collision.sceneQueries;
    int32_t queries = scene ? compiled->collision.queryCapacity : 0;
    return (EmitterAt){
        .queue = mvfxLayoutAdd(layout, (size_t)compiled->hostSpawnCapacity, sizeof(mvfxHostBirth),
                               alignof(mvfxHostBirth)),
        .forces = mvfxLayoutAdd(layout, (size_t)compiled->forceCount, sizeof(mvfxResolvedForce),
                                alignof(mvfxResolvedForce)),
        .rules = mvfxLayoutAdd(layout, (size_t)compiled->killRuleCount,
                               sizeof(mvfxResolvedKillRule), alignof(mvfxResolvedKillRule)),
        .colliders = mvfxLayoutAdd(layout, (size_t)colliders, sizeof(mvfxResolvedCollider),
                                   alignof(mvfxResolvedCollider)),
        .queries =
            mvfxLayoutAdd(layout, (size_t)queries, sizeof(mvfxSceneQuery), alignof(mvfxSceneQuery)),
        .events = mvfxLayoutAdd(layout, (size_t)compiled->eventCapacity, sizeof(mvfxEvent),
                                alignof(mvfxEvent)),
        .eventQueue = mvfxLayoutAdd(
            layout, compiled->eventSpawn.enabled ? (size_t)compiled->eventSpawn.capacity : 0,
            sizeof(mvfxHostBirth), alignof(mvfxHostBirth)),
    };
}

int32_t mvfxCapacityAt(const mvfxEffect* effect, int32_t emitter, int32_t quality)
{
    const mvfxQualityLevel* level = &effect->qualityLevels[quality];
    const mvfxCompiledEmitter* compiled = &effect->emitters[emitter];
    if (((level->omitted >> (uint32_t)emitter) & 1u) != 0)
    {
        return 0;
    }
    if (compiled->beam.enabled)
    {
        return compiled->capacity;
    }
    // Whole units: a multiple of the unit's points, at least one unit.
    int32_t points = compiled->unitPoints;
    int32_t whole = compiled->capacity / points;
    int32_t units = (int32_t)floor((double)whole * (double)level->capacityScale);
    return (units > 1 ? units : 1) * points;
}

int64_t mvfxParticlesAt(const mvfxEffect* effect, int32_t quality)
{
    int64_t particles = 0;
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        particles += mvfxCapacityAt(effect, e, quality);
    }
    return particles;
}

// Adds an emitter's contact room to the block, for one whose particles
// keep apart: its grid, then the places built from, the places moved and
// the radii, seven arrays of the capacity.
static void AddContacts(mvfxLayout* layout, const mvfxCompiledEmitter* compiled, int32_t capacity,
                        EmitterAt* at)
{
    if (!compiled->contacts.enabled)
    {
        return;
    }
    uint32_t size = GridTableSize(capacity);
    at->contactMask = size - 1u;
    at->contactTable = mvfxLayoutAdd(layout, (size_t)size + 1u, sizeof(int32_t), alignof(int32_t));
    at->contactKeys = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(uint32_t), alignof(uint32_t));
    at->contactIndex = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(int32_t), alignof(int32_t));
    at->contactPlaces = mvfxLayoutAdd(layout, 7u * (size_t)capacity, sizeof(float), alignof(float));
}

// Adds an emitter's plexus room to the block, for one with plexus links:
// its grid and two indices a link.
static void AddPlexus(mvfxLayout* layout, const mvfxCompiledEmitter* compiled, int32_t capacity,
                      EmitterAt* at)
{
    if (!compiled->plexus.enabled)
    {
        return;
    }
    uint32_t size = GridTableSize(capacity);
    at->plexusMask = size - 1u;
    at->plexusTable = mvfxLayoutAdd(layout, (size_t)size + 1u, sizeof(int32_t), alignof(int32_t));
    at->plexusKeys = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(uint32_t), alignof(uint32_t));
    at->plexusIndex = mvfxLayoutAdd(layout, (size_t)capacity, sizeof(int32_t), alignof(int32_t));
    at->plexusLinks = mvfxLayoutAdd(layout, 2u * (size_t)compiled->plexus.maxLinks, sizeof(int32_t),
                                    alignof(int32_t));
}

// Points each emitter's state at its parts of the block, with its
// capacity at the instance's level.
// Points an emitter's state at its rigid particles' room in the block.
static void StartRigid(mvfxEmitterState* state, unsigned char* block, const EmitterAt* at,
                       int32_t capacity)
{
    state->rigidStart = (float*)(block + at->rigidStart);
    state->rigidMass = (float*)(block + at->rigidMass);
    state->rigidFlags = (uint8_t*)(block + at->rigidFlags);
    state->rigidIslands = (int32_t*)(block + at->rigidIslands);
    state->rigidCandidates = (int32_t*)(block + at->rigidCandidates);
    state->rigidContacts = (mvfxBodyContact*)(block + at->rigidContacts);
    state->rigidCounts = (int32_t*)(block + at->rigidCounts);
    state->rigidGrid = (mvfxNeighborGrid){
        .table = (int32_t*)(block + at->rigidTable),
        .keys = (uint32_t*)(block + at->rigidKeys),
        .index = (int32_t*)(block + at->rigidIndex),
        .capacity = capacity,
        .mask = at->rigidMask,
    };
    state->rigidPairs = (mvfxPairContact*)(block + at->rigidPairs);
}

static void StartLiquid(mvfxEmitterState* state, unsigned char* block, const EmitterAt* at,
                        int32_t capacity)
{
    float* arrays = (float*)(block + at->liquidArrays);
    size_t stride = (size_t)capacity;
    state->liquid = (mvfxLiquidRoom){
        .grid =
            {
                .table = (int32_t*)(block + at->liquidTable),
                .keys = (uint32_t*)(block + at->liquidKeys),
                .index = (int32_t*)(block + at->liquidIndex),
                .capacity = capacity,
                .mask = at->liquidMask,
            },
        .predicted = arrays,
        .moved = arrays + 3u * stride,
        .multipliers = arrays + 6u * stride,
        .velocities = arrays + 7u * stride,
        .swirls = arrays + 10u * stride,
    };
}

static void StartStates(mvfxInstance* instance, unsigned char* block, mvfxBurstCursor* bursts,
                        const size_t particlesAt[], const EmitterAt at[],
                        const int32_t capacities[])
{
    const mvfxEffect* effect = instance->effect;
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        instance->emitters[e] = (mvfxEmitterState){
            .particles = block + particlesAt[e],
            .capacity = capacities[e],
            .arrayBytes = mvfxArrayBytes(capacities[e]),
            .bursts = &bursts[effect->emitters[e].firstBurst],
            .mesh = effect->emitters[e].shape.mesh,
            .hostQueue = (mvfxHostBirth*)(block + at[e].queue),
            .forces = (mvfxResolvedForce*)(block + at[e].forces),
            .killRules = (mvfxResolvedKillRule*)(block + at[e].rules),
            .colliders = (mvfxResolvedCollider*)(block + at[e].colliders),
            .queries = (mvfxSceneQuery*)(block + at[e].queries),
            .events = (mvfxEvent*)(block + at[e].events),
            .eventQueue = (mvfxHostBirth*)(block + at[e].eventQueue),
            .grid =
                {
                    .table = (int32_t*)(block + at[e].gridTable),
                    .keys = (uint32_t*)(block + at[e].gridKeys),
                    .index = (int32_t*)(block + at[e].gridIndex),
                    .values = (float*)(block + at[e].gridValues),
                    .capacity = capacities[e],
                    .mask = at[e].gridMask,
                },
        };
        float* places = (float*)(block + at[e].contactPlaces);
        size_t stride = (size_t)capacities[e];
        instance->emitters[e].contacts = (mvfxContactRoom){
            .grid =
                {
                    .table = (int32_t*)(block + at[e].contactTable),
                    .keys = (uint32_t*)(block + at[e].contactKeys),
                    .index = (int32_t*)(block + at[e].contactIndex),
                    .capacity = capacities[e],
                    .mask = at[e].contactMask,
                },
            .built = places,
            .moved = places + 3u * stride,
            .radii = places + 6u * stride,
        };
        instance->emitters[e].plexus = (mvfxPlexusRoom){
            .grid =
                {
                    .table = (int32_t*)(block + at[e].plexusTable),
                    .keys = (uint32_t*)(block + at[e].plexusKeys),
                    .index = (int32_t*)(block + at[e].plexusIndex),
                    .capacity = capacities[e],
                    .mask = at[e].plexusMask,
                },
            .links = (int32_t*)(block + at[e].plexusLinks),
        };
        instance->emitters[e].unitPrevious = (float*)(block + at[e].unitPrevious);
        instance->emitters[e].attachFrames = (mvfxAttachFrame*)(block + at[e].attachFrames);
        instance->emitters[e].attachTargets = (float*)(block + at[e].attachTargets);
        instance->emitters[e].selfGrid = (mvfxNeighborGrid){
            .table = (int32_t*)(block + at[e].selfTable),
            .keys = (uint32_t*)(block + at[e].selfKeys),
            .index = (int32_t*)(block + at[e].selfIndex),
            .capacity = capacities[e],
            .mask = at[e].selfMask,
        };
        instance->emitters[e].selfStart = (float*)(block + at[e].selfStart);
        instance->emitters[e].selfPairs = (int32_t*)(block + at[e].selfPairs);
        instance->emitters[e].selfCounts = (int32_t*)(block + at[e].selfCounts);
        StartRigid(&instance->emitters[e], block, &at[e], capacities[e]);
        StartLiquid(&instance->emitters[e], block, &at[e], capacities[e]);
    }
}

// Adds an emitter's rooms to the block: its queues and resolved parts,
// then the rooms of what its particles do.
static EmitterAt AddEmitterRooms(mvfxLayout* layout, const mvfxEffect* effect, int32_t e,
                                 const int32_t capacities[])
{
    const mvfxCompiledEmitter* compiled = &effect->emitters[e];
    EmitterAt at = AddEmitter(layout, compiled);
    AddGrid(layout, compiled, capacities[e], &at);
    AddContacts(layout, compiled, capacities[e], &at);
    AddPlexus(layout, compiled, capacities[e], &at);
    AddUnits(layout, effect, e, capacities, &at);
    AddRigid(layout, compiled, capacities[e], &at);
    AddLiquid(layout, compiled, capacities[e], &at);
    return at;
}

// Where an instance's gases' and water surfaces' buffers lie in its block.
typedef struct FieldsAt
{
    mvfxGasAt gases[MVFX_MAX_GASES];
    mvfxWaterAt waters[MVFX_MAX_WATERS];
} FieldsAt;

// Places an instance's gases' and water surfaces' buffers in its layout,
// a gas of an emitter's particles sized for its capacity.
static void AddFieldLayouts(mvfxLayout* layout, const mvfxEffect* effect, const int32_t* capacities,
                            FieldsAt* at)
{
    for (int32_t g = 0; g < effect->gasCount; ++g)
    {
        int32_t e = effect->gases[g].emitter;
        mvfxAddGasLayout(layout, &effect->gases[g], e >= 0 ? capacities[e] : 0, &at->gases[g]);
    }
    for (int32_t w = 0; w < effect->waterCount; ++w)
    {
        mvfxAddWaterLayout(layout, &effect->waters[w], &at->waters[w]);
    }
}

// Points an instance's gases' and water surfaces' states at their
// buffers in its block.
static void StartFields(mvfxInstance* instance, unsigned char* block, const int32_t* capacities,
                        const FieldsAt* at)
{
    const mvfxEffect* effect = instance->effect;
    for (int32_t g = 0; g < effect->gasCount; ++g)
    {
        int32_t e = effect->gases[g].emitter;
        mvfxStartGas(&instance->gases[g], &effect->gases[g], e >= 0 ? capacities[e] : 0, block,
                     &at->gases[g]);
        mvfxGasAreas(&effect->gases[g], &instance->gases[g]);
    }
    for (int32_t w = 0; w < effect->waterCount; ++w)
    {
        mvfxStartWater(&instance->waters[w], &effect->waters[w], block, &at->waters[w]);
    }
}

mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, int32_t quality,
                                      const mvfxTransform* transform, uint32_t seed,
                                      const mvfxAllocator* allocator)
{
    int32_t capacities[MVFX_MAX_EMITTERS] = {0};
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxInstance), MVFX_ATTRIBUTE_ALIGNMENT);
    size_t statesAt = mvfxLayoutAdd(&layout, (size_t)effect->emitterCount, sizeof(mvfxEmitterState),
                                    alignof(mvfxEmitterState));
    size_t particlesAt[MVFX_MAX_EMITTERS] = {0};
    int32_t largest = 0;
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        capacities[e] = mvfxCapacityAt(effect, e, quality);
        largest = capacities[e] > largest ? capacities[e] : largest;
    }
    size_t deathsAt = mvfxLayoutAdd(&layout, (size_t)largest, sizeof(int64_t), alignof(int64_t));
    size_t acceptedAt =
        mvfxLayoutAdd(&layout, (size_t)largest, sizeof(mvfxBirth), alignof(mvfxBirth));
    size_t killsAt = mvfxLayoutAdd(&layout, (size_t)largest, sizeof(uint8_t), alignof(uint8_t));
    size_t burstsAt = mvfxLayoutAdd(&layout, (size_t)effect->burstCount, sizeof(mvfxBurstCursor),
                                    alignof(mvfxBurstCursor));
    EmitterAt at[MVFX_MAX_EMITTERS] = {0};
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        at[e] = AddEmitterRooms(&layout, effect, e, capacities);
    }
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        particlesAt[e] = mvfxLayoutAdd(&layout, (size_t)effect->emitters[e].plan.arrayCount,
                                       mvfxArrayBytes(capacities[e]), MVFX_ATTRIBUTE_ALIGNMENT);
    }
    FieldsAt fieldsAt;
    AddFieldLayouts(&layout, effect, capacities, &fieldsAt);
    if (layout.overflow)
    {
        return nullptr;
    }
    unsigned char* block = mvfxAllocate(allocator, layout.size, MVFX_ATTRIBUTE_ALIGNMENT);
    if (block == nullptr)
    {
        return nullptr;
    }
    // The particle arrays are written before they are read, but a zeroed
    // block keeps the padding lanes' bits fixed for every platform's hash.
    memset(block, 0, layout.size);
    mvfxInstance* instance = (mvfxInstance*)block;
    *instance = (mvfxInstance){
        .effect = effect,
        .from = *transform,
        .to = *transform,
        .seed = seed,
        .quality = quality,
        .rateScale = effect->qualityLevels[quality].rateScale,
        .particles = mvfxParticlesAt(effect, quality),
        .emitters = (mvfxEmitterState*)(block + statesAt),
        .deathScratch = (int64_t*)(block + deathsAt),
        .acceptedScratch = (mvfxBirth*)(block + acceptedAt),
        .killScratch = block + killsAt,
        .blockBytes = layout.size,
    };
    for (int32_t p = 0; p < effect->parameterCount; ++p)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            instance->parameters[p][c] = effect->parameters[p][c];
        }
    }
    StartStates(instance, block, (mvfxBurstCursor*)(block + burstsAt), particlesAt, at, capacities);
    StartFields(instance, block, capacities, &fieldsAt);
    mvfxResetInstance(instance);
    return instance;
}

void mvfxResetInstance(mvfxInstance* instance)
{
    instance->clock = 0;
    instance->stepStart = 0;
    instance->heldTicks = 0;
    instance->unseenSteps = 0;
    instance->stopTick = INT64_MAX;
    instance->from = instance->to;
    instance->stepDistance = 0.0;
    instance->velocity[0] = 0.0f;
    instance->velocity[1] = 0.0f;
    instance->velocity[2] = 0.0f;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        mvfxEmitterState* state = &instance->emitters[e];
        state->count = 0;
        state->owed = 0;
        state->refused = 0;
        state->rate = (mvfxRateCursor){0};
        state->hostCount = 0;
        state->killed = 0;
        state->queryCount = 0;
        state->queriesDropped = 0;
        state->eventCount = 0;
        state->eventsDropped = 0;
        state->neighborsCapped = 0;
        state->contactsCapped = 0;
        state->plexus.linkCount = 0;
        state->linksDropped = 0;
        state->torn = 0;
        state->selfContactsCapped = 0;
        state->rigidContactsCapped = 0;
        state->rigidSearchesCapped = 0;
        state->hasBounds = false;
        state->ribbonCounter = 0;
        state->beamStrike = instance->effect->emitters[e].delay;
        state->distanceCarry = 0.0;
        state->distanceDelta = 0.0;
        state->distanceSkip = 0.0;
        state->distanceTake = 0;
        state->distanceNext = 0;
        for (int32_t k = 0; k < 3; ++k)
        {
            state->anchor[k] = instance->to.origin[k];
        }
        for (int32_t b = 0; b < instance->effect->emitters[e].burstCount; ++b)
        {
            state->bursts[b] = (mvfxBurstCursor){0};
        }
    }
    for (int32_t g = 0; g < instance->effect->gasCount; ++g)
    {
        mvfxGasState* gas = &instance->gases[g];
        mvfxClearGas(gas, &instance->effect->gases[g]);
        // A world gas's box stays where the instance starts.
        for (int32_t k = 0; k < 3; ++k)
        {
            gas->origin[k] = instance->to.origin[k];
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            gas->rotation[k] = instance->to.rotation[k];
        }
        gas->scale = instance->to.scale;
    }
    for (int32_t w = 0; w < instance->effect->waterCount; ++w)
    {
        mvfxWaterState* water = &instance->waters[w];
        mvfxClearWater(water, &instance->effect->waters[w]);
        // A world surface's grid stays where the instance starts.
        for (int32_t k = 0; k < 3; ++k)
        {
            water->origin[k] = instance->to.origin[k];
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            water->rotation[k] = instance->to.rotation[k];
        }
        water->scale = instance->to.scale;
    }
}

void mvfxReleaseInstance(mvfxInstance* instance, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, instance, instance->blockBytes, MVFX_ATTRIBUTE_ALIGNMENT);
}

// Hashes an emitter's events field by field (a struct's padding holds
// nothing fixed).
static uint64_t HashEvents(const mvfxEmitterState* state, uint64_t hash)
{
    hash = mvfxHash64(hash, &state->eventCount, (int32_t)sizeof state->eventCount);
    for (int32_t e = 0; e < state->eventCount; ++e)
    {
        const mvfxEvent* event = &state->events[e];
        uint8_t flags[2] = {event->kind, event->killed ? 1u : 0u};
        hash = mvfxHash64(hash, flags, (int32_t)sizeof flags);
        hash = mvfxHash64(hash, &event->serial, (int32_t)sizeof event->serial);
        hash = mvfxHash64(hash, &event->tick, (int32_t)sizeof event->tick);
        hash = mvfxHash64(hash, event->place, (int32_t)sizeof event->place);
        hash = mvfxHash64(hash, event->velocity, (int32_t)sizeof event->velocity);
        hash = mvfxHash64(hash, event->normal, (int32_t)sizeof event->normal);
        hash = mvfxHash64(hash, event->color, (int32_t)sizeof event->color);
        hash = mvfxHash64(hash, event->size, (int32_t)sizeof event->size);
    }
    return hash;
}

uint64_t mvfxHashInstance(const mvfxInstance* instance, uint64_t seed)
{
    uint64_t hash = mvfxHash64(seed, &instance->clock, (int32_t)sizeof instance->clock);
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        const mvfxEmitterState* state = &instance->emitters[e];
        hash = mvfxHash64(hash, &state->count, (int32_t)sizeof state->count);
        hash = mvfxHash64(hash, &state->owed, (int32_t)sizeof state->owed);
        hash = mvfxHash64(hash, &state->refused, (int32_t)sizeof state->refused);
        hash = mvfxHash64(hash, &state->neighborsCapped, (int32_t)sizeof state->neighborsCapped);
        hash = mvfxHash64(hash, &state->contactsCapped, (int32_t)sizeof state->contactsCapped);
        hash = mvfxHash64(hash, &state->linksDropped, (int32_t)sizeof state->linksDropped);
        hash = mvfxHash64(hash, &state->torn, (int32_t)sizeof state->torn);
        hash =
            mvfxHash64(hash, &state->selfContactsCapped, (int32_t)sizeof state->selfContactsCapped);
        hash = mvfxHash64(hash, &state->rigidContactsCapped,
                          (int32_t)sizeof state->rigidContactsCapped);
        hash = mvfxHash64(hash, &state->rigidSearchesCapped,
                          (int32_t)sizeof state->rigidSearchesCapped);
        hash = mvfxHash64(hash, &state->plexus.linkCount, (int32_t)sizeof state->plexus.linkCount);
        if (state->plexus.linkCount > 0)
        {
            hash = mvfxHash64(hash, state->plexus.links,
                              2 * state->plexus.linkCount * (int32_t)sizeof(int32_t));
        }
        hash = HashEvents(state, hash);
        hash = mvfxHash64(hash, &state->ribbonCounter, (int32_t)sizeof state->ribbonCounter);
        hash = mvfxHash64(hash, &state->beamStrike, (int32_t)sizeof state->beamStrike);
        if (state->hasBounds)
        {
            hash = mvfxHash64(hash, state->boundsMin, (int32_t)sizeof state->boundsMin);
            hash = mvfxHash64(hash, state->boundsMax, (int32_t)sizeof state->boundsMax);
        }
        const mvfxAttributePlan* plan = &instance->effect->emitters[e].plan;
        for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if (mvfxHasAttribute(plan, a))
            {
                hash = mvfxHash64(hash, mvfxAttributeArray(instance, e, a),
                                  state->count * (int32_t)sizeof(float));
            }
        }
    }
    for (int32_t g = 0; g < instance->effect->gasCount; ++g)
    {
        const mvfxCompiledGas* compiled = &instance->effect->gases[g];
        const mvfxGasState* gas = &instance->gases[g];
        // A surface holds its distance only: no faces, levels or other
        // scalars.
        bool surface = compiled->def.kind == mvfx_gasSurface;
        int32_t cells = (int32_t)(compiled->cells * (int64_t)sizeof(float));
        for (int32_t k = 0; k < 3; ++k)
        {
            hash = mvfxHash64(hash, gas->velocity[k][gas->current],
                              (int32_t)(compiled->faces[k] * (int64_t)sizeof(float)));
            hash = mvfxHash64(hash, gas->scalars[k][gas->current], surface && k > 0 ? 0 : cells);
        }
        hash = surface ? hash : mvfxHash64(hash, gas->levels[0].pressure, cells);
    }
    for (int32_t w = 0; w < instance->effect->waterCount; ++w)
    {
        const mvfxWaterState* water = &instance->waters[w];
        int32_t cells = (int32_t)(instance->effect->waters[w].cells * (int64_t)sizeof(float));
        hash = mvfxHash64(hash, water->heights[water->current], cells);
        hash = mvfxHash64(hash, water->rates, cells);
        // Shallow water's faces now; its ground is read anew each step.
        for (int32_t k = 0; water->ground != nullptr && k < 2; ++k)
        {
            int64_t faces = mvfxWaterFaces(instance->effect->waters[w].def.size, k);
            hash = mvfxHash64(hash, water->faces[k][water->faceCurrent],
                              (int32_t)(faces * (int64_t)sizeof(float)));
        }
    }
    return hash;
}

void mvfxTransformAt(const mvfxInstance* instance, int64_t tick, mvfxTransform* out)
{
    int64_t span = instance->clock - instance->stepStart;
    if (span <= 0 || tick >= instance->clock)
    {
        *out = instance->to;
        return;
    }
    double share = (double)(tick - instance->stepStart) / (double)span;
    const mvfxTransform* a = &instance->from;
    const mvfxTransform* b = &instance->to;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->origin[k] = a->origin[k] + (b->origin[k] - a->origin[k]) * share;
    }
    // Normalized linear interpolation, along the shorter way.
    float t = (float)share;
    float dot = a->rotation[0] * b->rotation[0] + a->rotation[1] * b->rotation[1] +
                a->rotation[2] * b->rotation[2] + a->rotation[3] * b->rotation[3];
    float sign = dot < 0.0f ? -1.0f : 1.0f;
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        out->rotation[k] = a->rotation[k] + (sign * b->rotation[k] - a->rotation[k]) * t;
        square += (double)out->rotation[k] * (double)out->rotation[k];
    }
    float scale = square > 0.0 ? (float)(1.0 / sqrt(square)) : 1.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        out->rotation[k] *= scale;
    }
    out->scale = a->scale + (b->scale - a->scale) * t;
}
