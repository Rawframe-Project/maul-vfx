// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_plan.h"

#include "context.h"
#include "force.h"
#include "fpenv.h"
#include "gas.h"
#include "gas_api.h"
#include "gas_solid.h"
#include "instance_api.h"
#include "plan_resolved.h"
#include "water_plan.h"

#include "maul-vfx/plan.h"

#include <math.h>

// Emitter e's particles' map into gas g's box as a plan hands it over
// (mvfxGasParticleMap), no rates.
static mvfxPlannedPour MapOf(const mvfxInstance* instance, int32_t g, int32_t e)
{
    static const double origin[3] = {0.0, 0.0, 0.0};
    mvfxSpaceMap map = mvfxGasParticleMap(instance, g, e);
    mvfxPlannedPour out = {.emitter = e, .sigma = map.sigma};
    mvfxMapPointDouble(&map, origin, out.origin);
    for (int32_t k = 0; k < 4; ++k)
    {
        out.rotation[k] = map.rotation[k];
    }
    return out;
}

// The emitters pouring into gas g as the CPU path's step would pour
// them (step.c's PoursInto): every one splatting into it, its live
// particles the simulation's to count; and the map of a liquid gas's
// emitter, or of the one a surface gas wraps.
static void PlanPours(const mvfxInstance* instance, int32_t g, mvfxGasState* state)
{
    state->planPourCount = 0;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        const mvfxGasSplat* splat = &instance->effect->emitters[e].gasSplat;
        if (!splat->enabled || splat->gas != g)
        {
            continue;
        }
        mvfxPlannedPour* pour = &state->planPours[state->planPourCount++];
        *pour = MapOf(instance, g, e);
        pour->rates[0] = splat->density;
        pour->rates[1] = splat->temperature;
        pour->rates[2] = splat->fuel;
        pour->drag = splat->drag;
    }
    const mvfxCompiledGas* gas = &instance->effect->gases[g];
    bool wraps = gas->def.kind == mvfx_gasLiquid || gas->def.kind == mvfx_gasSurface;
    state->planLiquid = wraps ? MapOf(instance, g, gas->emitter) : (mvfxPlannedPour){.emitter = -1};
}

void mvfxPlanGases(mvfxInstance* instance)
{
    for (int32_t g = 0; g < instance->effect->gasCount; ++g)
    {
        const mvfxCompiledGas* gas = &instance->effect->gases[g];
        mvfxGasState* state = &instance->gases[g];
        mvfxGasGravity(instance, g, state->planGravity);
        for (int32_t f = 0; f < gas->def.forceCount; ++f)
        {
            mvfxSpaceMap map = mvfxGasMap(instance, g, (mvfxSimulationSpace)gas->forces[f].space);
            state->planForces[f] = mvfxResolveForce(&gas->forces[f], &map, instance->parameters,
                                                    instance->seed, gas->forceGrids[f]);
        }
        if (gas->hasObstacles)
        {
            mvfxResolveGasObstacles(instance, g);
        }
        PlanPours(instance, g, state);
    }
    mvfxPlanWaters(instance);
    instance->gasesDue = false;
}

// Gas g's obstacle c as a plan hands it over: its own, then the instance's.
static mvfxPlannedCollider PlannedObstacle(const mvfxInstance* live, int32_t g,
                                           const mvfxResolvedCollider* resolved, int32_t c)
{
    const mvfxCompiledGas* gas = &live->effect->gases[g];
    int32_t own = gas->def.obstacleCount;
    mvfxGridId grid = c < own ? gas->obstacles[c].grid : live->hostColliders[c - own].grid;
    return mvfxPlannedColliderOf(resolved, grid);
}

// Gas g's plan of step s, as the CPU path takes that step: its span; a
// clear in the first step alone; its obstacles before the step the last
// plan's in the first, then where the first left them (the instance
// stands still after it), the same ones (moved by nothing, as the CPU
// path resolves them again each step); its forces resolved at its start;
// the rest as at the plan's end.
static void StepPlan(const mvfxInstance* live, int32_t g, int32_t s, mvfxGasPlan* planOut)
{
    const mvfxCompiledGas* compiled = &live->effect->gases[g];
    const mvfxGasState* state = &live->gases[g];
    int64_t start = 0;
    int64_t end = 0;
    mvfxPlanStepSpan(live, s, &start, &end);
    *planOut = (mvfxGasPlan){
        .start = start,
        .ticks = end - start,
        .stopTick = live->stopTick,
        .cleared = s == 0 && live->planCleared,
        .forceCount = compiled->def.forceCount,
        .ownObstacleCount = compiled->hasObstacles ? compiled->def.obstacleCount : 0,
        .obstaclesKnown = state->obstaclesKnown || s > 0,
        .pourCount = state->planPourCount,
        .liquid = state->planLiquid,
    };
    for (int32_t p = 0; p < state->planPourCount; ++p)
    {
        planOut->pours[p] = state->planPours[p];
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        planOut->gravity[k] = state->planGravity[k];
    }
    double time = (double)start / (double)MVFX_TICKS_PER_SECOND;
    for (int32_t f = 0; f < planOut->forceCount; ++f)
    {
        planOut->forces[f] = mvfxPlannedForceOf(&state->planForces[f], time);
    }
    if (compiled->hasObstacles && state->obstaclesResolved)
    {
        // After the first step the obstacles stand where it left them.
        int32_t now = state->obstaclesNow;
        int32_t before = s == 0 ? 1 - now : now;
        planOut->obstacleCount = state->obstacleCount[now];
        for (int32_t c = 0; c < planOut->obstacleCount; ++c)
        {
            planOut->obstacles[c] = PlannedObstacle(live, g, &state->obstacles[now][c], c);
            planOut->obstacleMoved[c] = s > 0 || mvfxGasObstacleMoved(state, c);
        }
        for (int32_t c = 0; planOut->obstaclesKnown && c < state->obstacleCount[before]; ++c)
        {
            planOut->obstaclesBefore[c] = PlannedObstacle(live, g, &state->obstacles[before][c], c);
        }
    }
}

// A planned instance holding gas g, or NULL with why not in result.
static const mvfxInstance* PlannedOf(const mvfxContext* context, mvfxInstanceId instance, int32_t g,
                                     mvfxResult* result)
{
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    *result = live == nullptr                        ? mvfx_errorStale
              : g < 0 || g >= live->effect->gasCount ? mvfx_errorInvalid
              : !live->planned                       ? mvfx_errorState
                                                     : mvfx_success;
    return *result == mvfx_success ? live : nullptr;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetGasPlanWork(const mvfxContext* context,
                                                            mvfxInstanceId instance, int32_t gas,
                                                            mvfxGasPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxGasPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = PlannedOf(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (mvfxPlanStepCount(live) > 1)
    {
        // Each step has its own plan.
        return mvfx_errorState;
    }
    StepPlan(live, gas, 0, planOut);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetGasPlan(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                   mvfxGasPlan* planOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetGasPlanWork(context, instance, gas, planOut);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetGasStepPlanWork(const mvfxContext* context,
                                                                mvfxInstanceId instance,
                                                                int32_t gas, int32_t step,
                                                                mvfxGasPlan* planOut)
{
    if (planOut != nullptr)
    {
        *planOut = (mvfxGasPlan){0};
    }
    if (context == nullptr || planOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = PlannedOf(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (step < 0 || step >= mvfxPlanStepCount(live))
    {
        return mvfx_errorInvalid;
    }
    StepPlan(live, gas, step, planOut);
    return mvfx_success;
}

mvfxResult mvfxInstance_GetGasStepPlan(const mvfxContext* context, mvfxInstanceId instance,
                                       int32_t gas, int32_t step, mvfxGasPlan* planOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetGasStepPlanWork(context, instance, gas, step, planOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// Whether nothing pours into gas g after tick: the instance stopped by
// then, and every emitter splatting into it had its last death.
static bool IsPouringOver(const mvfxInstance* live, int32_t g, int64_t tick)
{
    if (tick < live->stopTick)
    {
        return false;
    }
    for (int32_t e = 0; e < live->effect->emitterCount; ++e)
    {
        const mvfxGasSplat* splat = &live->effect->emitters[e].gasSplat;
        if (splat->enabled && splat->gas == g && live->emitters[e].lastDeath > tick)
        {
            return false;
        }
    }
    return true;
}

mvfxResult mvfxInstance_SetGasHolds(mvfxContext* context, mvfxInstanceId instance, int32_t gas,
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
    if (gas < 0 || gas >= live->effect->gasCount || tick < 0 || tick > live->clock)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorState;
    }
    mvfxGasState* state = &live->gases[gas];
    state->holds = holds || (state->holds && !IsPouringOver(live, gas, tick));
    return mvfx_success;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SetLiquidSpeedWork(mvfxContext* context,
                                                                mvfxInstanceId instance,
                                                                int32_t gas, int64_t tick,
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
    if (gas < 0 || gas >= live->effect->gasCount ||
        live->effect->gases[gas].def.kind != mvfx_gasLiquid || tick < 0 || tick > live->clock ||
        !isfinite(speed) || speed < 0.0f)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorState;
    }
    mvfxGasState* state = &live->gases[gas];
    if (tick >= state->reportedTick)
    {
        state->reportedSpeed = speed;
        state->reportedTick = tick;
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_SetLiquidSpeed(mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                       int64_t tick, float speed)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_SetLiquidSpeedWork(context, instance, gas, tick, speed);
    mvfxFloatEnvLeave(env);
    return result;
}
