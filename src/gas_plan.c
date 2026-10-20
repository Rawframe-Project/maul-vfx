// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "gas_plan.h"

#include "context.h"
#include "force.h"
#include "gas.h"
#include "gas_api.h"
#include "gas_solid.h"
#include "instance_api.h"
#include "plan_resolved.h"

#include "maul-vfx/plan.h"

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
    }
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

mvfxResult mvfxInstance_GetGasPlan(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
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
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (gas < 0 || gas >= live->effect->gasCount)
    {
        return mvfx_errorInvalid;
    }
    if (!live->planned)
    {
        return mvfx_errorState;
    }
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    const mvfxGasState* state = &live->gases[gas];
    *planOut = (mvfxGasPlan){
        .start = live->planStart,
        .ticks = live->clock - live->planStart,
        .stopTick = live->stopTick,
        .cleared = live->planCleared,
        .forceCount = compiled->def.forceCount,
        .ownObstacleCount = compiled->hasObstacles ? compiled->def.obstacleCount : 0,
        .obstaclesKnown = state->obstaclesKnown,
    };
    for (int32_t k = 0; k < 3; ++k)
    {
        planOut->gravity[k] = state->planGravity[k];
    }
    double time = (double)live->planStart / (double)MVFX_TICKS_PER_SECOND;
    for (int32_t f = 0; f < planOut->forceCount; ++f)
    {
        planOut->forces[f] = mvfxPlannedForceOf(&state->planForces[f], time);
    }
    if (compiled->hasObstacles && state->obstaclesResolved)
    {
        int32_t now = state->obstaclesNow;
        planOut->obstacleCount = state->obstacleCount[now];
        for (int32_t c = 0; c < planOut->obstacleCount; ++c)
        {
            planOut->obstacles[c] = PlannedObstacle(live, gas, &state->obstacles[now][c], c);
            planOut->obstacleMoved[c] = mvfxGasObstacleMoved(state, c);
        }
        for (int32_t c = 0; state->obstaclesKnown && c < state->obstacleCount[1 - now]; ++c)
        {
            planOut->obstaclesBefore[c] =
                PlannedObstacle(live, gas, &state->obstacles[1 - now][c], c);
        }
    }
    return mvfx_success;
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
