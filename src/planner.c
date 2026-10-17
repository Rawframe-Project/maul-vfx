// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance's steps. The births owed are counted and numbered as
// the CPU path's full step counts them (BirthsInTickOrder): every birth
// takes its serial, refused or not; each kept one's death tick is the one
// the CPU path's heap would hold. Room is left to the simulation, which
// alone knows what it holds: the plan refuses only what no simulation
// could take (past the capacity at one tick) and what it has no room for.

#include "planner.h"

#include "birth.h"
#include "emitter_step.h"
#include "spawn.h"

void mvfxBeginPlans(mvfxInstance* instance)
{
    instance->planStart = instance->clock;
    instance->planCleared = instance->clearPending;
    instance->clearPending = false;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        mvfxEmitterState* state = &instance->emitters[e];
        state->planCount = 0;
        state->planRefused = 0;
        for (int32_t k = 0; k < 3; ++k)
        {
            state->planAnchor[k] = state->anchor[k];
        }
    }
}

// Writes a kept birth into the emitter's plan.
static void Write(mvfxInstance* instance, int32_t emitter, uint64_t serial, int64_t tick,
                  int64_t batchIndex, int64_t batchCount)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    float inverseLife = mvfxInverseLifeOf(instance, emitter, serial, tick);
    int64_t death = mvfxDeathTick(tick, inverseLife);
    uint64_t deathFromStart = (uint64_t)(death - instance->planStart);
    state->plan[state->planCount++] = (mvfxPlannedBirth){
        .serialLow = (uint32_t)serial,
        .serialHigh = (uint32_t)(serial >> 32u),
        .tick = (uint32_t)(tick - instance->planStart),
        .deathLow = (uint32_t)deathFromStart,
        .deathHigh = (uint32_t)(deathFromStart >> 32u),
        .inverseLife = inverseLife,
        .batchIndex = (uint32_t)batchIndex,
        .batchCount = batchCount < UINT32_MAX ? (uint32_t)batchCount : UINT32_MAX,
    };
    state->lastDeath = death > state->lastDeath ? death : state->lastDeath;
}

void mvfxPlanEmitter(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (state->capacity == 0)
    {
        // Left out at the instance's quality level.
        return;
    }
    mvfxPlanDistance(instance, emitter);
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t room = 2 * (int64_t)state->capacity;
    int64_t tick = 0;
    int64_t count = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        // At one tick no more than the capacity fits, whatever has died.
        int64_t taken = count < state->capacity ? count : state->capacity;
        taken = taken < room - state->planCount ? taken : room - state->planCount;
        for (int64_t i = 0; i < taken; ++i)
        {
            Write(instance, emitter, state->owed + (uint64_t)i, tick, i, count);
        }
        state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)count);
        state->planRefused = mvfxSaturatingAdd(state->planRefused, (uint64_t)(count - taken));
        state->refused = mvfxSaturatingAdd(state->refused, (uint64_t)(count - taken));
    }
    mvfxSaveBirths(instance, &births);
    mvfxEndDistance(instance, emitter);
}
