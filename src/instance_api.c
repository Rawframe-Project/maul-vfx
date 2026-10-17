// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and reading instances on a context.

#include "instance_api.h"

#include "collide.h"
#include "context.h"
#include "effect_api.h"
#include "force.h"
#include "grid_api.h"
#include "instance.h"
#include "mesh.h"
#include "mesh_api.h"
#include "rigid_wake.h"
#include "slots.h"
#include "spawn.h"

#include <math.h>

mvfxInstanceDef mvfxDefaultInstanceDef(void)
{
    return (mvfxInstanceDef){
        .cookie = MVFX_INSTANCE_DEF_COOKIE,
        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        .scale = 1.0f,
    };
}

// Whether every coordinate of an origin is finite and at most 2^52 in
// size, where a double still holds a millimeter's fraction.
static bool IsOriginValid(const double origin[3])
{
    for (int32_t axis = 0; axis < 3; ++axis)
    {
        if (!(fabs(origin[axis]) <= 0x1p52))
        {
            return false;
        }
    }
    return true;
}

// Whether a rotation is a unit quaternion within 0.001, finite.
static bool IsRotationValid(const float rotation[4])
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        if (!(fabsf(rotation[k]) <= 1.001f))
        {
            return false;
        }
        square += (double)rotation[k] * (double)rotation[k];
    }
    return fabs(sqrt(square) - 1.0) <= 0.001;
}

static bool IsScaleValid(float scale)
{
    return scale >= MVFX_MIN_SCALE && scale <= MVFX_MAX_SCALE;
}

// A transform from valid values, its rotation normalized.
static mvfxTransform MakeTransform(const double origin[3], const float rotation[4], float scale)
{
    mvfxTransform transform = {.scale = scale};
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        square += (double)rotation[k] * (double)rotation[k];
    }
    double inverse = 1.0 / sqrt(square);
    for (int32_t k = 0; k < 4; ++k)
    {
        transform.rotation[k] = (float)((double)rotation[k] * inverse);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        transform.origin[k] = origin[k];
    }
    return transform;
}

// The seed an instance takes when its def gives none: its ordinal among
// the context's instances, hashed, never 0.
static uint32_t SeedFromOrdinal(uint64_t ordinal)
{
    uint64_t hash = mvfxHash64(MVFX_HASH_INIT, &ordinal, (int32_t)sizeof ordinal);
    uint32_t seed = (uint32_t)(hash ^ (hash >> 32u));
    return seed != 0 ? seed : 1;
}

// Whether an off-screen policy, and a throttle's interval, are valid.
static bool IsOffscreenValid(mvfxOffscreen offscreen, int32_t interval)
{
    return offscreen <= mvfx_offscreenPause &&
           (offscreen != mvfx_offscreenThrottle ||
            (interval >= 2 && interval <= MVFX_MAX_OFFSCREEN_INTERVAL));
}

// The first quality level from asked whose particles fit the context's
// budget beside those its instances hold; -1 for none.
static int32_t LevelThatFits(const mvfxContext* context, const mvfxEffect* effect, int32_t asked)
{
    for (int32_t q = asked; q < effect->qualityLevelCount; ++q)
    {
        if (context->particleBudget == 0 ||
            context->particlesHeld + mvfxParticlesAt(effect, q) <= context->particleBudget)
        {
            return q;
        }
    }
    return -1;
}

// Whether an effect's steps can be written as plans: none of what the
// core steps beside the particles (gases, water surfaces) or what waits on
// the particles held (a beam's strikes, births from events).
static bool CanBePlanned(const mvfxEffect* effect)
{
    if (effect->gasCount > 0 || effect->waterCount > 0)
    {
        return false;
    }
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        const mvfxCompiledEmitter* emitter = &effect->emitters[e];
        if (emitter->beam.enabled || emitter->eventSpawn.enabled)
        {
            return false;
        }
    }
    return true;
}

mvfxResult mvfxCreateInstance(mvfxContext* context, const mvfxInstanceDef* def,
                              mvfxInstanceId* instanceOut)
{
    if (context == nullptr || instanceOut == nullptr || def == nullptr ||
        def->cookie != MVFX_INSTANCE_DEF_COOKIE || !IsOriginValid(def->origin) ||
        !IsRotationValid(def->rotation) || !IsScaleValid(def->scale) || def->quality < 0 ||
        !IsOffscreenValid(def->offscreen, def->offscreenInterval))
    {
        if (instanceOut != nullptr)
        {
            *instanceOut = (mvfxInstanceId){0};
        }
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *instanceOut = (mvfxInstanceId){0};
    mvfxEffect* effect = mvfxContextEffect(context, def->effect);
    if (effect == nullptr)
    {
        return mvfx_errorStale;
    }
    if (def->quality >= effect->qualityLevelCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (def->planned && !CanBePlanned(effect))
    {
        return mvfx_errorState;
    }
    if (context->instances.firstFree == 0)
    {
        return mvfx_errorLimit;
    }
    int32_t quality = LevelThatFits(context, effect, def->quality);
    if (quality < 0)
    {
        return mvfx_errorCapacity;
    }
    uint32_t seed = def->seed != 0 ? def->seed : SeedFromOrdinal(context->instancesCreated);
    mvfxTransform transform = MakeTransform(def->origin, def->rotation, def->scale);
    mvfxInstance* instance = mvfxCreateInstanceBlock(effect, quality, &transform, seed,
                                                     def->planned, &context->allocator);
    if (instance == nullptr)
    {
        return mvfx_errorCapacity;
    }
    context->instancesCreated += 1;
    context->particlesHeld += instance->particles;
    instance->visible = true;
    instance->offscreen = def->offscreen;
    instance->offscreenInterval = def->offscreenInterval;
    effect->users += 1;
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        mvfxMesh* mesh = instance->emitters[e].mesh;
        mesh != nullptr ? (void)(mesh->users += 1) : (void)0;
    }
    uint32_t generation = 0;
    uint32_t index1 = mvfxTakeSlot(&context->instances, instance, &generation);
    *instanceOut = (mvfxInstanceId){index1, generation};
    return mvfx_success;
}

// Holds the grids of an instance's own colliders, or lets them go (by -1).
static void HoldHostGrids(mvfxInstance* instance, int32_t by)
{
    for (int32_t c = 0; c < instance->hostColliderCount; ++c)
    {
        mvfxGrid* grid = instance->hostColliderGrids[c];
        grid != nullptr ? (void)(grid->users += by) : (void)0;
    }
}

mvfxResult mvfxDestroyInstance(mvfxContext* context, mvfxInstanceId instance)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    live->effect->users -= 1;
    for (int32_t e = 0; e < live->effect->emitterCount; ++e)
    {
        mvfxMesh* mesh = live->emitters[e].mesh;
        mesh != nullptr ? (void)(mesh->users -= 1) : (void)0;
    }
    HoldHostGrids(live, -1);
    context->particlesHeld -= live->particles;
    mvfxReleaseInstance(live, &context->allocator);
    mvfxFreeSlot(&context->instances, instance.index1);
    return mvfx_success;
}

bool mvfxInstance_IsValid(const mvfxContext* context, mvfxInstanceId instance)
{
    return context != nullptr && mvfxContextInstance(context, instance) != nullptr;
}

mvfxResult mvfxInstance_GetEmitterStats(mvfxContext* context, mvfxInstanceId instance,
                                        int32_t emitter, mvfxEmitterStats* statsOut)
{
    if (statsOut != nullptr)
    {
        *statsOut = (mvfxEmitterStats){0};
    }
    if (context == nullptr || statsOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    *statsOut = (mvfxEmitterStats){
        .status = mvfxEmitterStatusOf(live, emitter),
        .live = state->count,
        .asleep = mvfxCountAsleep(live, emitter),
        .owed = state->owed,
        .refused = state->refused,
        .killed = state->killed,
        .queriesDropped = state->queriesDropped,
        .eventsDropped = state->eventsDropped,
        .neighborsCapped = state->neighborsCapped,
        .contactsCapped = state->contactsCapped,
        .linksDropped = state->linksDropped,
        .torn = state->torn,
        .selfContactsCapped = state->selfContactsCapped,
        .rigidContactsCapped = state->rigidContactsCapped,
        .rigidSearchesCapped = state->rigidSearchesCapped,
    };
    return mvfx_success;
}

mvfxResult mvfxInstance_GetEmitterPlan(const mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxEmitterPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxEmitterPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount)
    {
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorState;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    const mvfxTransform* from = &live->planFrom;
    const mvfxTransform* to = &live->to;
    *planOut = (mvfxEmitterPlan){
        .capacity = state->capacity,
        .seed = live->seed,
        .from = {.scale = from->scale},
        .to = {.scale = to->scale},
        .start = live->planStart,
        .ticks = live->clock - live->planStart,
        .births = state->plan,
        .birthCount = state->planCount,
        .refused = state->planRefused,
        .cleared = live->planCleared,
        .forceCount = live->effect->emitters[emitter].forceCount,
    };
    for (int32_t f = 0; f < planOut->forceCount; ++f)
    {
        planOut->forces[f] = mvfxPlannedForceOf(&state->forces[f]);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        planOut->anchorBefore[k] = state->planAnchor[k];
        planOut->anchorAfter[k] = state->anchor[k];
        planOut->from.origin[k] = from->origin[k];
        planOut->to.origin[k] = to->origin[k];
        planOut->velocity[k] = live->velocity[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        planOut->from.rotation[k] = from->rotation[k];
        planOut->to.rotation[k] = to->rotation[k];
    }
    return mvfx_success;
}

mvfxEmitterStatus mvfxEmitterStatusOf(const mvfxInstance* instance, int32_t emitter)
{
    if (mvfxHasBirthsAhead(instance, emitter))
    {
        return instance->clock < instance->effect->emitters[emitter].delay ? mvfx_emitterDelayed
                                                                           : mvfx_emitterActive;
    }
    const mvfxEmitterState* state = &instance->emitters[emitter];
    bool holds = state->count > 0 || (instance->planned && state->lastDeath > instance->clock);
    return holds ? mvfx_emitterStopping : mvfx_emitterStopped;
}

bool mvfxIsInstanceFinished(const mvfxInstance* instance)
{
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        if (mvfxEmitterStatusOf(instance, e) != mvfx_emitterStopped)
        {
            return false;
        }
    }
    // A gas runs until its instance stops, and then until it holds
    // nothing.
    for (int32_t g = 0; g < instance->effect->gasCount; ++g)
    {
        if (instance->clock < instance->stopTick || instance->gases[g].holds)
        {
            return false;
        }
    }
    // So does a water surface, and while it has pushes to take (crossings
    // wait only within a step).
    for (int32_t w = 0; w < instance->effect->waterCount; ++w)
    {
        const mvfxWaterState* water = &instance->waters[w];
        if (instance->clock < instance->stopTick || water->holds || water->pushCount > 0)
        {
            return false;
        }
    }
    return true;
}

bool mvfxInstance_IsFinished(const mvfxContext* context, mvfxInstanceId instance)
{
    const mvfxInstance* live =
        context != nullptr ? mvfxContextInstance(context, instance) : nullptr;
    return live != nullptr && mvfxIsInstanceFinished(live);
}

mvfxResult mvfxInstance_Stop(mvfxContext* context, mvfxInstanceId instance, mvfxStopMode mode)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mode != mvfx_stopEmitting && mode != mvfx_stopAndClear)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    live->stopTick = live->clock < live->stopTick ? live->clock : live->stopTick;
    if (mode == mvfx_stopAndClear)
    {
        for (int32_t e = 0; e < live->effect->emitterCount; ++e)
        {
            live->emitters[e].count = 0;
            live->emitters[e].lastDeath = 0;
        }
        live->clearPending = true;
        for (int32_t g = 0; g < live->effect->gasCount; ++g)
        {
            mvfxClearGas(&live->gases[g], &live->effect->gases[g]);
        }
        for (int32_t w = 0; w < live->effect->waterCount; ++w)
        {
            mvfxClearWater(&live->waters[w], &live->effect->waters[w]);
        }
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_Restart(mvfxContext* context, mvfxInstanceId instance)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResetInstance(live);
    return mvfx_success;
}

// Advances the ribbon counter of an emitter, or with -1 of every emitter
// drawn as ribbons.
static void BreakRibbons(mvfxInstance* instance, int32_t emitter)
{
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        if ((emitter == -1 || emitter == e) &&
            instance->effect->emitters[e].ribbonIds != mvfx_ribbonNone)
        {
            instance->emitters[e].ribbonCounter += 1;
        }
    }
}

mvfxResult mvfxInstance_SetTransform(mvfxContext* context, mvfxInstanceId instance,
                                     const double origin[3], const float rotation[4], float scale,
                                     bool teleport)
{
    if (context == nullptr || origin == nullptr || rotation == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (!IsOriginValid(origin) || !IsRotationValid(rotation) || !IsScaleValid(scale))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    live->to = MakeTransform(origin, rotation, scale);
    if (teleport)
    {
        live->from = live->to;
        BreakRibbons(live, -1);
        // Gases' obstacles seen across the jump did not move.
        for (int32_t g = 0; g < live->effect->gasCount; ++g)
        {
            live->gases[g].obstaclesResolved = false;
        }
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_SetVisible(mvfxContext* context, mvfxInstanceId instance, bool visible)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    live->visible = visible;
    return mvfx_success;
}

mvfxResult mvfxInstance_SetOffscreen(mvfxContext* context, mvfxInstanceId instance,
                                     mvfxOffscreen offscreen, int32_t interval)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (!IsOffscreenValid(offscreen, interval))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    live->offscreen = offscreen;
    live->offscreenInterval = interval;
    return mvfx_success;
}

// Whether count values from values, of components each, are finite and
// at most bound in size; true for NULL.
static bool AreValuesWithin(const float* values, int64_t count, float bound)
{
    for (int64_t i = 0; values != nullptr && i < count; ++i)
    {
        if (!(fabsf(values[i]) <= bound))
        {
            return false;
        }
    }
    return true;
}

// Whether a spawn request is valid for an emitter with room in its queue.
static bool IsSpawnValid(const mvfxCompiledEmitter* emitter, const mvfxSpawnRequest* request)
{
    if (request->count < 1 || request->positions == nullptr ||
        (request->colors != nullptr && !mvfxHasAttribute(&emitter->plan, mvfx_attributeColorR)) ||
        (request->sizes != nullptr && !mvfxHasAttribute(&emitter->plan, mvfx_attributeSizeX)))
    {
        return false;
    }
    int64_t count = request->count;
    for (int64_t i = 0; i < 3 * count; ++i)
    {
        if (!(fabs(request->positions[i]) <= 0x1p52))
        {
            return false;
        }
    }
    bool hosted = emitter->unitPoints > 1 && emitter->unit.attach == mvfx_attachHost;
    for (int64_t i = 0; request->attachFrames != nullptr && i < count; ++i)
    {
        int32_t frame = request->attachFrames[i];
        if (!hosted || frame < 0 || frame >= emitter->unit.attachFrameCapacity)
        {
            return false;
        }
    }
    return AreValuesWithin(request->velocities, 3 * count, MVFX_MAX_EXTENT) &&
           AreValuesWithin(request->colors, 4 * count, MVFX_MAX_VALUE) &&
           AreValuesWithin(request->sizes, 3 * count, MVFX_MAX_VALUE);
}

// Copies a request's i-th particle, flagging the values it gives.
static mvfxHostBirth HostBirthOf(const mvfxSpawnRequest* request, int32_t i)
{
    mvfxHostBirth host = {.frame = request->attachFrames != nullptr ? request->attachFrames[i] : 0};
    for (int32_t k = 0; k < 3; ++k)
    {
        host.position[k] = request->positions[3 * i + k];
    }
    if (request->velocities != nullptr)
    {
        host.given |= mvfx_givenVelocity;
        for (int32_t k = 0; k < 3; ++k)
        {
            host.velocity[k] = request->velocities[3 * i + k];
        }
    }
    if (request->colors != nullptr)
    {
        host.given |= mvfx_givenColor;
        for (int32_t k = 0; k < 4; ++k)
        {
            host.color[k] = request->colors[4 * i + k];
        }
    }
    if (request->sizes != nullptr)
    {
        host.given |= mvfx_givenSize;
        for (int32_t k = 0; k < 3; ++k)
        {
            host.size[k] = request->sizes[3 * i + k];
        }
    }
    return host;
}

mvfxResult mvfxInstance_Spawn(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              const mvfxSpawnRequest* request)
{
    if (context == nullptr || request == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount ||
        !IsSpawnValid(&live->effect->emitters[emitter], request))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (live->planned)
    {
        // Its births are planned from its emitters' own sources alone.
        return mvfx_errorState;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    if (request->count > live->effect->emitters[emitter].hostSpawnCapacity - state->hostCount)
    {
        return mvfx_errorCapacity;
    }
    for (int32_t i = 0; i < request->count; ++i)
    {
        state->hostQueue[state->hostCount++] = HostBirthOf(request, i);
    }
    return mvfx_success;
}

// Whether a host's frame is valid: its place finite and at most 2^52 in
// size, its orientation of length 1 within 1e-3.
static bool IsAttachFrameValid(const mvfxAttachFrame* frame)
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        square += (double)frame->rotation[k] * (double)frame->rotation[k];
    }
    bool placed = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        placed = placed && fabs(frame->position[k]) <= 0x1p52;
    }
    return placed && fabs(sqrt(square) - 1.0) <= 1e-3;
}

mvfxResult mvfxInstance_SetAttachFrames(mvfxContext* context, mvfxInstanceId instance,
                                        int32_t emitter, const mvfxAttachFrame* frames,
                                        int32_t count)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    const mvfxCompiledEmitter* compiled = emitter >= 0 && emitter < live->effect->emitterCount
                                              ? &live->effect->emitters[emitter]
                                              : nullptr;
    bool valid = compiled != nullptr && compiled->unitPoints > 1 &&
                 compiled->unit.attach == mvfx_attachHost && count >= 0 &&
                 count <= compiled->unit.attachFrameCapacity && (count == 0 || frames != nullptr);
    for (int32_t f = 0; valid && f < count; ++f)
    {
        valid = IsAttachFrameValid(&frames[f]);
    }
    if (!valid)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    for (int32_t f = 0; f < count; ++f)
    {
        state->attachFrames[f] = frames[f];
    }
    state->attachFrameCount = count;
    return mvfx_success;
}

mvfxResult mvfxInstance_SetColliders(mvfxContext* context, mvfxInstanceId instance,
                                     const mvfxCollider* colliders, int32_t count)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    bool valid = count >= 0 && count <= MVFX_MAX_COLLIDERS && (count == 0 || colliders != nullptr);
    for (int32_t c = 0; valid && c < count; ++c)
    {
        valid = mvfxIsColliderValid(&colliders[c]);
    }
    if (!valid)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxGrid* grids[MVFX_MAX_COLLIDERS] = {0};
    for (int32_t c = 0; c < count; ++c)
    {
        mvfxResult found = mvfxFindColliderGrid(context, &colliders[c], &grids[c]);
        if (found != mvfx_success)
        {
            return found;
        }
    }
    HoldHostGrids(live, -1);
    for (int32_t c = 0; c < count; ++c)
    {
        live->hostColliders[c] = mvfxCompileCollider(&colliders[c]);
        live->hostColliderGrids[c] = grids[c];
    }
    live->hostColliderCount = count;
    HoldHostGrids(live, 1);
    return mvfx_success;
}

mvfxResult mvfxInstance_SetDepthImage(mvfxContext* context, mvfxInstanceId instance,
                                      const mvfxDepthImage* image)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (image == nullptr)
    {
        live->depthSerial += 1u;
        live->hasDepthView = false;
        return mvfx_success;
    }
    mvfxDepthView view;
    if (!mvfxPrepareDepthView(image, &view))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    live->depthSerial += 1u;
    live->depthView = view;
    live->hasDepthView = true;
    return mvfx_success;
}

mvfxResult mvfxInstance_WakeBodies(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                   const float center[3], float radius)
{
    bool valid = context != nullptr && center != nullptr && isfinite(center[0]) &&
                 isfinite(center[1]) && isfinite(center[2]) && radius >= 0.0f;
    if (!valid)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxWakeWithin(live, emitter, center, radius);
    return mvfx_success;
}

mvfxResult mvfxInstance_SetEmitterMesh(mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxMeshId mesh)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    mvfxMesh* bound = mvfxContextMesh(context, mesh);
    if (live == nullptr || bound == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount ||
        live->effect->emitters[emitter].shape.kind != mvfx_shapeMesh ||
        !mvfxMeshHasMode(bound, live->effect->emitters[emitter].shape.meshMode))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    state->mesh->users -= 1;
    bound->users += 1;
    state->mesh = bound;
    return mvfx_success;
}

// The live instance an id names and whether a parameter index is in
// range, counting misuse; the result the call returns otherwise.
static mvfxResult FindParameter(mvfxContext* context, mvfxInstanceId instance, int32_t parameter,
                                const void* value, mvfxInstance** liveOut)
{
    if (context == nullptr || value == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *liveOut = mvfxContextInstance(context, instance);
    if (*liveOut == nullptr)
    {
        return mvfx_errorStale;
    }
    if (parameter < 0 || parameter >= (*liveOut)->effect->parameterCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_SetParameter(mvfxContext* context, mvfxInstanceId instance,
                                     int32_t parameter, const float value[4])
{
    mvfxInstance* live = nullptr;
    mvfxResult result = FindParameter(context, instance, parameter, value, &live);
    if (result != mvfx_success)
    {
        return result;
    }
    for (int32_t c = 0; c < 4; ++c)
    {
        if (!isfinite(value[c]))
        {
            mvfxCountMisuse(context);
            return mvfx_errorInvalid;
        }
    }
    for (int32_t c = 0; c < 4; ++c)
    {
        live->parameters[parameter][c] = value[c];
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_GetParameter(mvfxContext* context, mvfxInstanceId instance,
                                     int32_t parameter, float valueOut[4])
{
    if (valueOut != nullptr)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            valueOut[c] = 0.0f;
        }
    }
    mvfxInstance* live = nullptr;
    mvfxResult result = FindParameter(context, instance, parameter, valueOut, &live);
    if (result != mvfx_success)
    {
        return result;
    }
    for (int32_t c = 0; c < 4; ++c)
    {
        valueOut[c] = live->parameters[parameter][c];
    }
    return mvfx_success;
}

mvfxInstance* mvfxContextInstance(const mvfxContext* context, mvfxInstanceId instance)
{
    return mvfxSlotObject(&context->instances, instance.index1, instance.generation);
}

// The live instance and the emitter an id and an index name, counting
// misuse; the result the call returns otherwise.
mvfxResult mvfxFindEmitter(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                           mvfxInstance** liveOut)
{
    *liveOut = mvfxContextInstance(context, instance);
    if (*liveOut == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= (*liveOut)->effect->emitterCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    return mvfx_success;
}

mvfxResult mvfxFindEmitters(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                            mvfxInstance** liveOut)
{
    if (emitter != -1)
    {
        return mvfxFindEmitter(context, instance, emitter, liveOut);
    }
    *liveOut = mvfxContextInstance(context, instance);
    return *liveOut != nullptr ? mvfx_success : mvfx_errorStale;
}

mvfxResult mvfxInstance_BreakRibbons(mvfxContext* context, mvfxInstanceId instance, int32_t emitter)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitters(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    BreakRibbons(live, emitter);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetQuality(mvfxContext* context, mvfxInstanceId instance, int32_t* levelOut)
{
    if (context == nullptr || levelOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *levelOut = 0;
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    *levelOut = live->quality;
    return mvfx_success;
}
