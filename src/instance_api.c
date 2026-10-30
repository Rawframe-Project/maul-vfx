// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and reading instances on a context.

#include "instance_api.h"

#include "collide.h"
#include "context.h"
#include "effect_api.h"
#include "fpenv.h"
#include "grid_api.h"
#include "instance.h"
#include "mesh.h"
#include "plan_resolved.h"
#include "refusal.h"
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

// A scale's status: finite, and from MVFX_MIN_SCALE to MVFX_MAX_SCALE.
static mvfxResult CheckScale(float scale)
{
    return mvfxBounded(scale, MVFX_MIN_SCALE, MVFX_MAX_SCALE);
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

// Whether an off-screen policy, and a throttle's interval, are valid:
// mvfx_errorLimit for an interval past MVFX_MAX_OFFSCREEN_INTERVAL.
static mvfxResult CheckOffscreen(mvfxOffscreen offscreen, int32_t interval)
{
    if (offscreen > mvfx_offscreenPause)
    {
        return mvfx_errorInvalid;
    }
    return offscreen == mvfx_offscreenThrottle
               ? mvfxCountUpTo(interval, 2, MVFX_MAX_OFFSCREEN_INTERVAL)
               : mvfx_success;
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

static MVFX_NOINLINE mvfxResult mvfxCreateInstanceWork(mvfxContext* context,
                                                       const mvfxInstanceDef* def,
                                                       mvfxInstanceId* instanceOut)
{
    mvfxResult valid = mvfx_errorInvalid;
    if (def != nullptr && def->cookie == MVFX_INSTANCE_DEF_COOKIE && IsOriginValid(def->origin) &&
        IsRotationValid(def->rotation) && def->quality >= 0)
    {
        valid = mvfxFirst(CheckScale(def->scale),
                          CheckOffscreen(def->offscreen, def->offscreenInterval));
    }
    valid = context == nullptr || instanceOut == nullptr ? mvfx_errorInvalid : valid;
    if (valid != mvfx_success)
    {
        if (instanceOut != nullptr)
        {
            *instanceOut = (mvfxInstanceId){0};
        }
        // A def past a bound is data a correct program may pass: no misuse.
        if (context != nullptr && valid == mvfx_errorInvalid)
        {
            mvfxCountMisuse(context);
        }
        return valid;
    }
    *instanceOut = (mvfxInstanceId){0};
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
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
    if (context->instances.firstFree == 0)
    {
        return mvfx_errorCapacity;
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

mvfxResult mvfxCreateInstance(mvfxContext* context, const mvfxInstanceDef* def,
                              mvfxInstanceId* instanceOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxCreateInstanceWork(context, def, instanceOut);
    mvfxFloatEnvLeave(env);
    return result;
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
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

static MVFX_NOINLINE mvfxResult mvfxInstance_GetEmitterStatsWork(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t emitter,
                                                                 mvfxEmitterStats* statsOut)
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
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
        .valuesKilled = state->valuesKilled,
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

mvfxResult mvfxInstance_GetEmitterStats(mvfxContext* context, mvfxInstanceId instance,
                                        int32_t emitter, mvfxEmitterStats* statsOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetEmitterStatsWork(context, instance, emitter, statsOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// The ticks from a plan's start whose events give an emitter births.
static void SpawnWindow(const mvfxInstance* instance, int32_t emitter, mvfxEmitterPlan* plan)
{
    int64_t first = 1;
    int64_t last = 0;
    if (instance->effect->emitters[emitter].eventSpawn.enabled)
    {
        mvfxEventSpawnWindow(instance, emitter, plan->start, plan->start + plan->ticks, &first,
                             &last);
        first -= plan->start;
        last -= plan->start;
    }
    plan->spawnFirst = first;
    plan->spawnLast = last;
}

mvfxResult mvfxInstance_GetEmitterMesh(const mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxMeshView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxMeshView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount ||
        live->emitters[emitter].mesh == nullptr)
    {
        return mvfx_errorInvalid;
    }
    *viewOut = mvfxViewOfMesh(live->emitters[emitter].mesh);
    return mvfx_success;
}

// An emitter's plan of step s as the CPU path takes it: its span and its
// births; the instance's move over the first step alone, still after it.
static void StepPlan(const mvfxInstance* live, int32_t emitter, int32_t s, mvfxEmitterPlan* planOut)
{
    const mvfxEmitterState* state = &live->emitters[emitter];
    bool split = live->stepped && live->planSteps > 0;
    int32_t last = live->planSteps - 1;
    int64_t start = 0;
    int64_t end = 0;
    mvfxPlanStepSpan(live, s, &start, &end);
    int32_t first = split ? state->planStepFirst[s] : 0;
    int32_t past = split && s < last ? state->planStepFirst[s + 1] : state->planCount;
    uint64_t refusedBefore = split && s > 0 ? state->planStepRefused[s - 1] : 0;
    const mvfxTransform* from = s == 0 ? &live->planFrom : &live->to;
    const mvfxTransform* to = &live->to;
    *planOut = (mvfxEmitterPlan){
        .capacity = state->capacity,
        .seed = live->seed,
        .ribbonCounter = state->ribbonCounter,
        .from = {.scale = from->scale},
        .to = {.scale = to->scale},
        .start = start,
        .ticks = end - start,
        .births = state->plan + first,
        .birthCount = past - first,
        .refused = (split ? state->planStepRefused[s] : state->planRefused) - refusedBefore,
        // The host's particles are born at the first step's start.
        .hostBirths = state->planHosts,
        .hostBirthCount = s == 0 ? state->planHostCount : 0,
        .attachFrames = state->planFrames,
        .attachFrameCount = live->effect->emitters[emitter].unit.attach == mvfx_attachHost
                                ? state->attachFrameCount
                                : 0,
        .cleared = s == 0 && live->planCleared,
        .owed = split ? state->planStepOwed[s] : state->owed,
    };
    SpawnWindow(live, emitter, planOut);
    mvfxPlanResolved(live, emitter, from, planOut);
    planOut->ribbonHead = mvfxRibbonHeadPlace(live, emitter, planOut->ribbonHeadPlace);
    // Its neighbor grid's mask at its capacity (a planned instance's own
    // grid is laid out for no particles).
    planOut->gridMask = live->effect->emitters[emitter].gridCell > 0.0f
                            ? mvfxGridTableSize(state->capacity) - 1u
                            : 0u;
    const float* velocity = live->stepped ? live->planVelocity : live->velocity;
    for (int32_t k = 0; k < 3; ++k)
    {
        planOut->anchorBefore[k] = s == 0 ? state->planAnchor[k] : state->anchor[k];
        planOut->anchorAfter[k] = state->anchor[k];
        planOut->from.origin[k] = from->origin[k];
        planOut->to.origin[k] = to->origin[k];
        planOut->velocity[k] = s == 0 ? velocity[k] : 0.0f;
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        planOut->from.rotation[k] = from->rotation[k];
        planOut->to.rotation[k] = to->rotation[k];
    }
}

// The planned instance an emitter's plan query reads; NULL with the
// result on failure.
static const mvfxInstance* PlannedOf(const mvfxContext* context, mvfxInstanceId instance,
                                     int32_t emitter, mvfxResult* result)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    *result = live == nullptr                                        ? mvfx_errorStale
              : emitter < 0 || emitter >= live->effect->emitterCount ? mvfx_errorInvalid
              : !live->planned                                       ? mvfx_errorUnsupported
                                                                     : mvfx_success;
    return *result == mvfx_success ? live : nullptr;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetEmitterPlanWork(const mvfxContext* context,
                                                                mvfxInstanceId instance,
                                                                int32_t emitter,
                                                                mvfxEmitterPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxEmitterPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = PlannedOf(context, instance, emitter, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (mvfxPlanStepCount(live) > 1)
    {
        // Each step has its own plan.
        return mvfx_errorState;
    }
    StepPlan(live, emitter, 0, planOut);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetEmitterPlan(const mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxEmitterPlan* planOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetEmitterPlanWork(context, instance, emitter, planOut);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetEmitterStepPlanWork(const mvfxContext* context,
                                                                    mvfxInstanceId instance,
                                                                    int32_t emitter, int32_t step,
                                                                    mvfxEmitterPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxEmitterPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = PlannedOf(context, instance, emitter, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (step < 0 || step >= mvfxPlanStepCount(live))
    {
        return mvfx_errorInvalid;
    }
    StepPlan(live, emitter, step, planOut);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetEmitterStepPlan(const mvfxContext* context, mvfxInstanceId instance,
                                           int32_t emitter, int32_t step, mvfxEmitterPlan* planOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result =
        mvfxInstance_GetEmitterStepPlanWork(context, instance, emitter, step, planOut);
    mvfxFloatEnvLeave(env);
    return result;
}

mvfxResult mvfxInstance_GetPlanSteps(const mvfxContext* context, mvfxInstanceId instance,
                                     int32_t* countOut)
{
    if (countOut != nullptr)
    {
        *countOut = 0;
    }
    if (context == nullptr || countOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (!live->planned)
    {
        return mvfx_errorUnsupported;
    }
    *countOut = mvfxPlanStepCount(live);
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

static MVFX_NOINLINE mvfxResult mvfxInstance_StopWork(mvfxContext* context, mvfxInstanceId instance,
                                                      mvfxStopMode mode)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
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
            // A planned surface's reports of the steps before count no more.
            live->waters[w].countedTick = live->clock;
        }
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_Stop(mvfxContext* context, mvfxInstanceId instance, mvfxStopMode mode)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_StopWork(context, instance, mode);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_RestartWork(mvfxContext* context,
                                                         mvfxInstanceId instance)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResetInstance(live);
    return mvfx_success;
}

mvfxResult mvfxInstance_Restart(mvfxContext* context, mvfxInstanceId instance)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_RestartWork(context, instance);
    mvfxFloatEnvLeave(env);
    return result;
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

static MVFX_NOINLINE mvfxResult mvfxInstance_SetTransformWork(mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              const double origin[3],
                                                              const float rotation[4], float scale,
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResult valid =
        IsOriginValid(origin) && IsRotationValid(rotation) ? CheckScale(scale) : mvfx_errorInvalid;
    if (valid != mvfx_success)
    {
        mvfxCountMisuse(context);
        return valid;
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

mvfxResult mvfxInstance_SetTransform(mvfxContext* context, mvfxInstanceId instance,
                                     const double origin[3], const float rotation[4], float scale,
                                     bool teleport)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result =
        mvfxInstance_SetTransformWork(context, instance, origin, rotation, scale, teleport);
    mvfxFloatEnvLeave(env);
    return result;
}

mvfxResult mvfxInstance_SetVisible(mvfxContext* context, mvfxInstanceId instance, bool visible)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResult valid = CheckOffscreen(offscreen, interval);
    if (valid != mvfx_success)
    {
        mvfxCountMisuse(context);
        return valid;
    }
    live->offscreen = offscreen;
    live->offscreenInterval = interval;
    return mvfx_success;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SetCollidersWork(mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              const mvfxCollider* colliders,
                                                              int32_t count)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxResult valid = count > 0 && colliders == nullptr
                           ? mvfx_errorInvalid
                           : mvfxCountUpTo(count, 0, MVFX_MAX_COLLIDERS);
    for (int32_t c = 0; valid == mvfx_success && c < count; ++c)
    {
        valid = mvfxCheckCollider(&colliders[c]);
    }
    if (valid != mvfx_success)
    {
        mvfxCountMisuse(context);
        return valid;
    }
    mvfxGrid* grids[MVFX_MAX_COLLIDERS] = {0};
    for (int32_t c = 0; c < count; ++c)
    {
        mvfxResult found = mvfxFindColliderGrid(context, &colliders[c], &grids[c]);
        if (found != mvfx_success)
        {
            found == mvfx_errorInvalid ? mvfxCountMisuse(context) : (void)0;
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

mvfxResult mvfxInstance_SetColliders(mvfxContext* context, mvfxInstanceId instance,
                                     const mvfxCollider* colliders, int32_t count)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_SetCollidersWork(context, instance, colliders, count);
    mvfxFloatEnvLeave(env);
    return result;
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
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
    mvfxResult reach = mvfxCheckParameterReach(value, live->effect->parameterReach[parameter]);
    if (reach != mvfx_success)
    {
        mvfxCountMisuse(context);
        return reach;
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
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
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
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
    if (levelOut != nullptr)
    {
        *levelOut = 0;
    }
    if (context == nullptr || levelOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    *levelOut = live->quality;
    return mvfx_success;
}
