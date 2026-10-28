// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance's steps. The births owed are counted and numbered as
// the CPU path's full step counts them (BirthsInTickOrder): every birth
// takes its serial, refused or not; each kept one's death tick is the one
// the CPU path's heap would hold. Room is left to the simulation, which
// alone knows what it holds: the plan refuses only what no simulation
// could take (past the capacity at one tick) and what it has no room for.
// An emitter that spawns from events is numbered by the simulation, which
// alone builds the births events give: the plan counts its own and the
// host's births, and its last death is bounded instead.

#include "planner.h"

#include "attach.h"
#include "birth.h"
#include "emitter_step.h"
#include "integrate.h"
#include "spawn.h"

#include <math.h>

void mvfxBeginPlans(mvfxInstance* instance)
{
    instance->planStart = instance->clock;
    instance->planBase = instance->clock;
    instance->planSteps = 0;
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

void mvfxBeginPlanStep(mvfxInstance* instance)
{
    // A context's step takes at most two splits of the instance's step;
    // past the most, a step joins the last (never reached).
    if (instance->planSteps == MVFX_MAX_PLAN_STEPS)
    {
        return;
    }
    int32_t s = instance->planSteps++;
    instance->planStepStart[s] = instance->stepStart;
    instance->planBase = instance->stepStart;
    if (s == 0)
    {
        // The instance moves over its first step alone.
        for (int32_t k = 0; k < 3; ++k)
        {
            instance->planVelocity[k] = instance->velocity[k];
        }
    }
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        instance->emitters[e].planStepFirst[s] = instance->emitters[e].planCount;
    }
}

void mvfxEndPlanStep(mvfxInstance* instance)
{
    int32_t s = instance->planSteps - 1;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        mvfxEmitterState* state = &instance->emitters[e];
        state->planStepOwed[s] = state->owed;
        state->planStepRefused[s] = state->planRefused;
    }
}

// Whether the simulation numbers an emitter's births: it spawns from
// events.
static bool NumberedElsewhere(const mvfxInstance* instance, int32_t emitter)
{
    return instance->effect->emitters[emitter].eventSpawn.enabled;
}

// The births an emitter holds at most: its particles, or for an emitter
// of units its units (a birth is a unit, its points born together).
static int32_t UnitCapacity(const mvfxInstance* instance, int32_t emitter)
{
    return instance->emitters[emitter].capacity / instance->effect->emitters[emitter].unitPoints;
}

// Writes a kept birth with the given inverse life into the emitter's
// plan, the host's particle it is (all ones for one of its own); its
// death tick and inverse life 0 when the simulation numbers it. Past the
// plan's room (twice the births it holds, which a plan of many steps can
// reach with a beam's strikes) the birth is refused, still owed.
static void WriteLived(mvfxInstance* instance, int32_t emitter, uint64_t serial, int64_t tick,
                       int64_t batchIndex, int64_t batchCount, uint32_t host, float inverseLife)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (state->planCount >= 2 * UnitCapacity(instance, emitter))
    {
        state->planRefused = mvfxSaturatingAdd(state->planRefused, 1u);
        state->refused = mvfxSaturatingAdd(state->refused, 1u);
        return;
    }
    bool elsewhere = NumberedElsewhere(instance, emitter);
    inverseLife = elsewhere ? 0.0f : inverseLife;
    int64_t death = elsewhere ? instance->planBase : mvfxDeathTick(tick, inverseLife);
    uint64_t deathFromStart = (uint64_t)(death - instance->planBase);
    state->plan[state->planCount++] = (mvfxPlannedBirth){
        .serialLow = (uint32_t)serial,
        .serialHigh = (uint32_t)(serial >> 32u),
        .tick = (uint32_t)(tick - instance->planBase),
        .deathLow = (uint32_t)deathFromStart,
        .deathHigh = (uint32_t)(deathFromStart >> 32u),
        .inverseLife = inverseLife,
        .batchIndex = (uint32_t)batchIndex,
        .batchCount = batchCount < UINT32_MAX ? (uint32_t)batchCount : UINT32_MAX,
        .sourceAge = mvfxSourceAge(&instance->effect->emitters[emitter], tick),
        .host = host,
        .eventKeyLow = 0,
        .eventKeyHigh = 0,
    };
    state->lastDeath = death > state->lastDeath ? death : state->lastDeath;
}

// Writes a kept birth, its life drawn from its serial.
static void Write(mvfxInstance* instance, int32_t emitter, uint64_t serial, int64_t tick,
                  int64_t batchIndex, int64_t batchCount, uint32_t host)
{
    float inverseLife = NumberedElsewhere(instance, emitter)
                            ? 0.0f
                            : mvfxInverseLifeOf(instance, emitter, serial, tick);
    WriteLived(instance, emitter, serial, tick, batchIndex, batchCount, host, inverseLife);
}

// The values a host's particle gives as a plan names them, and how its
// velocity is given where the simulation draws it.
static uint8_t GivenOf(const mvfxInstance* instance, int32_t emitter, const mvfxHostBirth* host)
{
    uint32_t given = ((host->given & mvfx_givenColor) != 0 ? MVFX_HOST_GIVES_COLOR : 0u) |
                     ((host->given & mvfx_givenSize) != 0 ? MVFX_HOST_GIVES_SIZE : 0u);
    if (NumberedElsewhere(instance, emitter))
    {
        given |= ((host->given & mvfx_givenVelocity) != 0 ? MVFX_HOST_GIVES_VELOCITY : 0u) |
                 ((host->given & mvfx_givenAddedVelocity) != 0 ? MVFX_HOST_ADDS_VELOCITY : 0u);
    }
    return (uint8_t)given;
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
    int32_t units = UnitCapacity(instance, emitter);
    int32_t taken = state->hostCount < units ? state->hostCount : units;
    for (int32_t h = 0; h < taken; ++h)
    {
        const mvfxHostBirth* host = &state->hostQueue[h];
        uint64_t serial = state->owed + (uint64_t)h;
        mvfxPlannedHostBirth* out = &state->planHosts[state->planHostCount];
        *out =
            (mvfxPlannedHostBirth){.given = GivenOf(instance, emitter, host), .frame = host->frame};
        for (int32_t k = 0; k < 3; ++k)
        {
            out->position[k] = host->position[k];
            out->size[k] = host->size[k];
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            out->color[k] = host->color[k];
        }
        if (NumberedElsewhere(instance, emitter))
        {
            // Drawn by the simulation at the serial it decides.
            for (int32_t k = 0; k < 3; ++k)
            {
                out->velocity[k] = host->velocity[k];
            }
        }
        else
        {
            mvfxHostVelocity(instance, emitter, host, serial, start, out->velocity);
        }
        Write(instance, emitter, serial, start, h, state->hostCount,
              (uint32_t)state->planHostCount++);
    }
    uint64_t refused = (uint64_t)(state->hostCount - taken);
    state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)state->hostCount);
    state->planRefused = mvfxSaturatingAdd(state->planRefused, refused);
    state->refused = mvfxSaturatingAdd(state->refused, refused);
    state->hostCount = 0;
}

// The ticks the longest life a birth of the emitter could draw now spans,
// and one more: its lifetime's largest value times its curve's largest
// sample or its parameter, held as a drawn life is.
static int64_t LongestLifeTicks(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledScalar* lifetime = &instance->effect->emitters[emitter].lifetime;
    double longest = fmax(fabs((double)lifetime->min), fabs((double)lifetime->max));
    if (lifetime->kind == mvfx_sourceCurve)
    {
        double factor = 0.0;
        for (int32_t s = 0; s < MVFX_CURVE_SAMPLES; ++s)
        {
            factor = fmax(factor, fabs((double)lifetime->curve->samples[s]));
        }
        longest *= factor;
    }
    else if (lifetime->kind == mvfx_sourceParameter)
    {
        longest *= fabs((double)instance->parameters[lifetime->parameter][0]);
    }
    longest = fmin(fmax(longest, 0x1p-20), (double)MVFX_MAX_LIFETIME);
    return (int64_t)ceil(longest * (double)MVFX_TICKS_PER_SECOND) + 1;
}

// An emitter numbered by the simulation has no death ticks in its plan:
// when it took births within the plan, or its source may have written
// events where they give births (it held particles or took births), its
// particles may live until the plan's end and the longest life after.
static void BoundLastDeath(mvfxInstance* instance, int32_t emitter)
{
    if (!NumberedElsewhere(instance, emitter))
    {
        return;
    }
    mvfxEmitterState* state = &instance->emitters[emitter];
    const mvfxEmitterState* source =
        &instance->emitters[instance->effect->emitters[emitter].eventSpawn.source];
    int64_t first = 0;
    int64_t last = 0;
    mvfxEventSpawnWindow(instance, emitter, instance->planStart, instance->clock, &first, &last);
    bool sourceWrites = first <= last && (source->lastDeath > first || source->planCount > 0);
    if (state->planCount > 0 || sourceWrites)
    {
        int64_t bound = instance->clock + LongestLifeTicks(instance, emitter);
        state->lastDeath = bound > state->lastDeath ? bound : state->lastDeath;
    }
}

// A beam's strike by the plan's end, as the step strikes it: by the
// clock alone, since a strike's tick is the last one's death, by which
// every point has ended. A strike fits: the capacity holds its points.
static void PlanStrike(mvfxInstance* instance, int32_t emitter)
{
    mvfxStrike strike;
    if (!mvfxNextStrike(instance, emitter, &strike))
    {
        return;
    }
    for (int32_t i = 0; i < strike.points; ++i)
    {
        WriteLived(instance, emitter, mvfxSaturatingAdd(strike.first, (uint64_t)i), strike.tick, i,
                   strike.points, UINT32_MAX, strike.inverseLife);
    }
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
    mvfxPlanAttachFrames(instance, emitter);
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int32_t units = UnitCapacity(instance, emitter);
    int64_t room = 2 * (int64_t)units;
    int64_t tick = 0;
    int64_t count = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        // At one tick no more than the capacity fits, whatever has died.
        int64_t taken = count < units ? count : units;
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
    PlanStrike(instance, emitter);
    BoundLastDeath(instance, emitter);
    mvfxEndDistance(instance, emitter);
}
