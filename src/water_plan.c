// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "water_plan.h"

#include "box.h"
#include "context.h"
#include "effect.h"
#include "effect_api.h"
#include "force.h"
#include "fpenv.h"
#include "instance_api.h"
#include "plan_resolved.h"
#include "water.h"
#include "water_api.h"
#include "water_ground.h"
#include "water_shallow.h"
#include "water_step.h"

#include "maul-vfx/plan.h"

#include <float.h>
#include <math.h>

static void StepPlan(const mvfxInstance* live, int32_t w, int32_t s, mvfxWaterPlan* planOut);

void mvfxPlanWaters(mvfxInstance* instance)
{
    for (int32_t w = 0; w < instance->effect->waterCount; ++w)
    {
        const mvfxCompiledWater* water = &instance->effect->waters[w];
        mvfxWaterState* state = &instance->waters[w];
        // The pushes as the CPU path's step maps them, where the surface
        // stands at the step's end.
        mvfxBoxPlace place = mvfxWaterPlace(instance, w);
        mvfxSpaceMap map = mvfxBoxMap(instance, &place, mvfx_spaceWorld);
        for (int32_t p = 0; p < state->pushCount; ++p)
        {
            const mvfxWaterPush* push = &state->pushes[p];
            float local[3];
            mvfxMapPointDouble(&map, push->point, local);
            state->planPushes[p] = (mvfxPlannedWaterPush){.at = {local[0], local[2]},
                                                          .radius = push->radius * map.sigma,
                                                          .depth = push->depth * map.sigma};
        }
        state->planPushCount = state->pushCount;
        state->pushCount = 0;
        mvfxResolvedCollider terrain = {0};
        mvfxGridId grid = {0};
        state->planHasTerrain = water->def.model == mvfx_waterShallow &&
                                mvfxResolveWaterTerrain(instance, w, &terrain, &grid);
        state->planTerrain = state->planHasTerrain ? mvfxPlannedColliderOf(&terrain, grid)
                                                   : (mvfxPlannedCollider){0};
        // Its steps slowed, counted as the CPU path's steps count them.
        for (int32_t s = 0; s < mvfxPlanStepCount(instance); ++s)
        {
            mvfxWaterPlan plan;
            StepPlan(instance, w, s, &plan);
            state->stepsSlowed += plan.slowed && plan.substeps > 0 ? 1u : 0u;
        }
        // The simulation fills it at this step.
        state->filled = true;
    }
}

// Surface w's plan of step s, as the CPU path takes that step: its span
// and substeps; a clear and the pushes in the first step alone; its
// sources' depths over its span; its terrain as at the plan's end.
static void StepPlan(const mvfxInstance* live, int32_t w, int32_t s, mvfxWaterPlan* planOut)
{
    const mvfxCompiledWater* water = &live->effect->waters[w];
    const mvfxWaterState* state = &live->waters[w];
    int64_t start = 0;
    int64_t end = 0;
    mvfxPlanStepSpan(live, s, &start, &end);
    double seconds = (double)(end - start) / (double)MVFX_TICKS_PER_SECOND;
    bool shallow = water->def.model == mvfx_waterShallow;
    *planOut = (mvfxWaterPlan){
        .start = start,
        .ticks = end - start,
        .stopTick = live->stopTick,
        .cleared = s == 0 && live->planCleared,
        .pushCount = s == 0 ? state->planPushCount : 0,
        .hasTerrain = state->planHasTerrain,
        .terrain = state->planTerrain,
    };
    double scale = (double)mvfxWaterPlace(live, w).scale;
    planOut->volumeScale = scale * scale * scale;
    planOut->substeps = shallow ? mvfxShallowSubsteps(water, state->stepSpeed, seconds,
                                                      &planOut->dt, &planOut->slowed)
                                : mvfxWaveSubsteps(water, seconds, &planOut->dt, &planOut->slowed);
    for (int32_t p = 0; p < planOut->pushCount; ++p)
    {
        const mvfxPlannedWaterPush* push = &state->planPushes[p];
        planOut->pushes[p] = *push;
        planOut->pushSank +=
            shallow ? 0.0 : mvfxSinkWaves(water, nullptr, push->at, push->radius, push->depth);
    }
    // As mvfxPourWaterSources adds them, while the instance plays.
    for (int32_t k = 0; shallow && start < live->stopTick && k < water->def.sourceCount; ++k)
    {
        planOut->sourceDepths[k] = (float)((double)water->sources[k].rate * seconds);
    }
}

mvfxResult mvfxEffect_GetWaterView(const mvfxContext* context, mvfxEffectId effect, int32_t water,
                                   mvfxWaterView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxWaterView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxEffect* live = mvfxContextEffect(context, effect);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (water < 0 || water >= live->waterCount)
    {
        return mvfx_errorInvalid;
    }
    const mvfxCompiledWater* w = &live->waters[water];
    *viewOut = (mvfxWaterView){.def = w->def, .cells = w->cells};
    viewOut->def.sources = w->def.sourceCount > 0 ? w->sources : nullptr;
    viewOut->def.terrain = w->terrain;
    if (w->def.model == mvfx_waterShallow)
    {
        viewOut->faces[0] = mvfxWaterFaces(w->def.size, 0);
        viewOut->faces[1] = mvfxWaterFaces(w->def.size, 1);
    }
    return mvfx_success;
}

// A planned instance holding water w, or NULL with why not in result.
static const mvfxInstance* PlannedOf(const mvfxContext* context, mvfxInstanceId instance, int32_t w,
                                     mvfxResult* result)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    *result = live == nullptr                          ? mvfx_errorStale
              : w < 0 || w >= live->effect->waterCount ? mvfx_errorInvalid
              : !live->planned                         ? mvfx_errorUnsupported
                                                       : mvfx_success;
    return *result == mvfx_success ? live : nullptr;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetWaterPlanWork(const mvfxContext* context,
                                                              mvfxInstanceId instance,
                                                              int32_t water, mvfxWaterPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxWaterPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = PlannedOf(context, instance, water, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (mvfxPlanStepCount(live) > 1)
    {
        // Each step has its own plan.
        return mvfx_errorState;
    }
    StepPlan(live, water, 0, planOut);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetWaterPlan(const mvfxContext* context, mvfxInstanceId instance,
                                     int32_t water, mvfxWaterPlan* planOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetWaterPlanWork(context, instance, water, planOut);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetWaterStepPlanWork(const mvfxContext* context,
                                                                  mvfxInstanceId instance,
                                                                  int32_t water, int32_t step,
                                                                  mvfxWaterPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxWaterPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = PlannedOf(context, instance, water, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (step < 0 || step >= mvfxPlanStepCount(live))
    {
        return mvfx_errorInvalid;
    }
    StepPlan(live, water, step, planOut);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetWaterStepPlan(const mvfxContext* context, mvfxInstanceId instance,
                                         int32_t water, int32_t step, mvfxWaterPlan* planOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetWaterStepPlanWork(context, instance, water, step, planOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// Whether nothing moves surface w after tick but its own motion: the
// instance stopped by then (shallow water's sources pour while it plays),
// and every emitter whose particles meet it had its last death.
static bool IsStirringOver(const mvfxInstance* live, int32_t w, int64_t tick)
{
    if (tick < live->stopTick)
    {
        return false;
    }
    for (int32_t e = 0; e < live->effect->emitterCount; ++e)
    {
        const mvfxWaterCoupling* coupling = &live->effect->emitters[e].water;
        if (coupling->enabled && coupling->water == w && live->emitters[e].lastDeath > tick)
        {
            return false;
        }
    }
    return true;
}

mvfxResult mvfxInstance_SetWaterHolds(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                                      int64_t tick, bool holds)
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
    if (water < 0 || water >= live->effect->waterCount || tick < 0 || tick > live->clock)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorUnsupported;
    }
    mvfxWaterState* state = &live->waters[water];
    state->holds = holds || (state->holds && !IsStirringOver(live, water, tick));
    return mvfx_success;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SetWaterCountsWork(mvfxContext* context,
                                                                mvfxInstanceId instance,
                                                                int32_t water, int64_t tick,
                                                                float reach, uint64_t facesHeld,
                                                                double volumePoured)
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
    if (water < 0 || water >= live->effect->waterCount || tick < 0 || tick > live->clock ||
        !(reach >= 0.0f && reach <= FLT_MAX) || !(volumePoured >= 0.0 && volumePoured <= DBL_MAX))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorUnsupported;
    }
    mvfxWaterState* state = &live->waters[water];
    if (tick > state->countedTick)
    {
        state->reach = reach;
        state->facesHeld += facesHeld;
        state->volumePoured += volumePoured;
        state->countedTick = tick;
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_SetWaterCounts(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                                       int64_t tick, float reach, uint64_t facesHeld,
                                       double volumePoured)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_SetWaterCountsWork(context, instance, water, tick, reach,
                                                        facesHeld, volumePoured);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SetWaterSpeedWork(mvfxContext* context,
                                                               mvfxInstanceId instance,
                                                               int32_t water, int64_t tick,
                                                               float speed)
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
    if (water < 0 || water >= live->effect->waterCount ||
        live->effect->waters[water].def.model != mvfx_waterShallow || tick < 0 ||
        tick > live->clock || !isfinite(speed) || speed < 0.0f)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorUnsupported;
    }
    mvfxWaterState* state = &live->waters[water];
    if (tick >= state->reportedTick)
    {
        state->reportedSpeed = speed;
        state->reportedTick = tick;
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_SetWaterSpeed(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                                      int64_t tick, float speed)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_SetWaterSpeedWork(context, instance, water, tick, speed);
    mvfxFloatEnvLeave(env);
    return result;
}
