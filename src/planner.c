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
#include "integrate.h"
#include "spawn.h"

void mvfxBeginPlans(mvfxInstance* instance)
{
    instance->planStart = instance->clock;
    instance->planFrom = instance->from;
    instance->planCleared = instance->clearPending;
    instance->clearPending = false;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        mvfxEmitterState* state = &instance->emitters[e];
        state->planCount = 0;
        state->planRefused = 0;
        state->planHostCount = 0;
        for (int32_t k = 0; k < 3; ++k)
        {
            state->planAnchor[k] = state->anchor[k];
        }
    }
}

// Writes a kept birth into the emitter's plan, the host's particle it is
// (all ones for one of its own).
static void Write(mvfxInstance* instance, int32_t emitter, uint64_t serial, int64_t tick,
                  int64_t batchIndex, int64_t batchCount, uint32_t host)
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
        .sourceAge = mvfxSourceAge(&instance->effect->emitters[emitter], tick),
        .host = host,
    };
    state->lastDeath = death > state->lastDeath ? death : state->lastDeath;
}

// Writes the host's queued particles, born at the step's start before its
// other births, as the CPU path's: each takes a serial. They are the
// plan's first births (the host queues between the context's steps); past
// the capacity at the instance's quality level no simulation could take
// them at one tick, so they are refused. The room a live particle leaves
// is the simulation's to judge, as for any birth.
static void PlanHostBirths(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    int64_t start = instance->stepStart;
    int32_t taken = state->hostCount < state->capacity ? state->hostCount : state->capacity;
    for (int32_t h = 0; h < taken; ++h)
    {
        const mvfxHostBirth* host = &state->hostQueue[h];
        uint64_t serial = state->owed + (uint64_t)h;
        mvfxPlannedHostBirth* out = &state->planHosts[state->planHostCount];
        *out = (mvfxPlannedHostBirth){
            .given = (uint8_t)(((host->given & mvfx_givenColor) != 0 ? MVFX_HOST_GIVES_COLOR : 0u) |
                               ((host->given & mvfx_givenSize) != 0 ? MVFX_HOST_GIVES_SIZE : 0u))};
        for (int32_t k = 0; k < 3; ++k)
        {
            out->position[k] = host->position[k];
            out->size[k] = host->size[k];
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            out->color[k] = host->color[k];
        }
        mvfxHostVelocity(instance, emitter, host, serial, start, out->velocity);
        Write(instance, emitter, serial, start, h, state->hostCount,
              (uint32_t)state->planHostCount++);
    }
    uint64_t refused = (uint64_t)(state->hostCount - taken);
    state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)state->hostCount);
    state->planRefused = mvfxSaturatingAdd(state->planRefused, refused);
    state->refused = mvfxSaturatingAdd(state->refused, refused);
    state->hostCount = 0;
}

void mvfxPlanEmitter(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (state->capacity == 0)
    {
        // Left out at the instance's quality level: the host's particles
        // are refused.
        state->refused = mvfxSaturatingAdd(state->refused, (uint64_t)state->hostCount);
        state->hostCount = 0;
        return;
    }
    mvfxPlanDistance(instance, emitter);
    // The forces and colliders of the step, held over the plan.
    mvfxResolveEmitter(instance, emitter);
    PlanHostBirths(instance, emitter);
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
            Write(instance, emitter, state->owed + (uint64_t)i, tick, i, count, UINT32_MAX);
        }
        state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)count);
        state->planRefused = mvfxSaturatingAdd(state->planRefused, (uint64_t)(count - taken));
        state->refused = mvfxSaturatingAdd(state->refused, (uint64_t)(count - taken));
    }
    mvfxSaveBirths(instance, &births);
    mvfxEndDistance(instance, emitter);
}
