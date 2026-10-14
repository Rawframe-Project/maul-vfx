// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Births from an emitter's rate and bursts, merged by tick.

#include "spawn.h"

#include "random.h"

#include <math.h>

// The salt that sets bursts' draws apart from any other module's.
#define BURST_SALT 0x42525354u

int64_t mvfxRateBirthTick(const mvfxCompiledEmitter* emitter, float scale, uint64_t loop,
                          uint64_t j)
{
    if (!(scale > 0.0f))
    {
        return MVFX_NO_TICK;
    }
    // The j-th birth of a rate times scale comes where the rate's own
    // integral reaches (j + 1) / scale.
    double reach = ((double)j + 1.0) / (double)scale - (double)loop * emitter->loopIntegral;
    double ticks = 0.0;
    if (emitter->rateCurve == nullptr)
    {
        if (emitter->rate <= 0.0f)
        {
            return MVFX_NO_TICK;
        }
        ticks = reach * (double)MVFX_TICKS_PER_SECOND / (double)emitter->rate;
    }
    else
    {
        // The integral over the normalized age, times the loop's seconds,
        // reaches the target at the normalized age that solves it.
        double seconds = (double)emitter->duration / (double)MVFX_TICKS_PER_SECOND;
        double age = mvfxCurveIntegralTime(&emitter->rateCurve->table,
                                           &emitter->rateCurve->integral, reach / seconds);
        ticks = age * (double)emitter->duration;
    }
    ticks = ceil(ticks);
    if (!(ticks < 0x1p62))
    {
        return MVFX_NO_TICK;
    }
    return ticks > 0.0 ? (int64_t)ticks : 0;
}

// The first tick of a loop.
static int64_t LoopStart(const mvfxCompiledEmitter* emitter, uint64_t loop)
{
    return emitter->delay + (int64_t)loop * emitter->duration;
}

// Whether a loop is past the emitter's last.
static bool IsPastLoops(const mvfxCompiledEmitter* emitter, uint64_t loop)
{
    return emitter->loops > 0 && loop >= (uint64_t)emitter->loops;
}

// The tick of the rate's next birth, moving its cursor past loops it has
// no more births in; MVFX_NO_TICK when none starts by limit.
static int64_t PeekRate(const mvfxCompiledEmitter* emitter, float scale, mvfxRateCursor* cursor,
                        int64_t limit)
{
    if (emitter->rate <= 0.0f)
    {
        return MVFX_NO_TICK;
    }
    for (;;)
    {
        if (IsPastLoops(emitter, cursor->loop))
        {
            return MVFX_NO_TICK;
        }
        int64_t start = LoopStart(emitter, cursor->loop);
        if (start > limit)
        {
            return MVFX_NO_TICK;
        }
        int64_t tick = mvfxRateBirthTick(emitter, scale, cursor->loop, cursor->next);
        if (emitter->duration == 0)
        {
            return tick == MVFX_NO_TICK ? MVFX_NO_TICK : start + tick;
        }
        tick = tick < cursor->last ? cursor->last : tick;
        if (tick <= emitter->duration)
        {
            return start + tick;
        }
        cursor->loop += 1;
        cursor->last = 0;
    }
}

// The tick of a burst's next firing, moving its cursor past loops it has
// no more firings in; MVFX_NO_TICK when none starts by limit.
static int64_t PeekBurst(const mvfxCompiledEmitter* emitter, const mvfxCompiledBurst* burst,
                         mvfxBurstCursor* cursor, int64_t limit)
{
    for (;;)
    {
        if (IsPastLoops(emitter, cursor->loop))
        {
            return MVFX_NO_TICK;
        }
        int64_t start = LoopStart(emitter, cursor->loop);
        if (start > limit)
        {
            return MVFX_NO_TICK;
        }
        if (burst->cycles == 0 || cursor->firing < (uint64_t)burst->cycles)
        {
            int64_t tick = burst->time + (int64_t)cursor->firing * burst->interval;
            if (emitter->duration == 0 || tick < emitter->duration)
            {
                return start + tick;
            }
        }
        if (emitter->duration == 0)
        {
            return MVFX_NO_TICK;
        }
        *cursor = (mvfxBurstCursor){.loop = cursor->loop + 1};
    }
}

// Whether a tick lies where the emitter spawns: after its delay, within
// its loops, not past a stop.
static bool IsSpawning(const mvfxInstance* instance, const mvfxCompiledEmitter* emitter,
                       int64_t tick)
{
    return tick > emitter->delay && tick <= instance->stopTick &&
           (emitter->loops == 0 || tick <= LoopStart(emitter, (uint64_t)emitter->loops));
}

// The tick of the next distance birth the step takes, passing over those
// that fall where the emitter does not spawn; MVFX_NO_TICK when none is
// left. The loop is bounded by the step's take, at most the capacity.
static int64_t PeekDistance(const mvfxInstance* instance, int32_t emitter, mvfxBirths* births)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    int64_t span = instance->clock - instance->stepStart;
    while (births->distanceNext < state->distanceTake && span > 0)
    {
        // The birth's place along the step: where carry plus its share of
        // the step's integral reaches its index in the step plus 1.
        double index = state->distanceSkip + (double)births->distanceNext;
        double share = (index + 1.0 - state->distanceCarry) / state->distanceDelta;
        share = share < 1.0 ? share : 1.0;
        int64_t tick = instance->stepStart + (int64_t)ceil(share * (double)span);
        tick = tick <= instance->stepStart ? instance->stepStart + 1 : tick;
        tick = tick > instance->clock ? instance->clock : tick;
        if (IsSpawning(instance, compiled, tick))
        {
            return tick;
        }
        births->distanceNext += 1;
    }
    return MVFX_NO_TICK;
}

// The particles a burst's firing spawns: none when its chance fails, else
// a count drawn from its range, by pcg4d at the firing.
static int64_t BurstCount(const mvfxInstance* instance, int32_t emitter, int32_t burstIndex,
                          const mvfxCompiledBurst* burst, const mvfxBurstCursor* cursor)
{
    mvfxWords4 words = mvfxPcg4d((mvfxWords4){
        instance->seed,
        ((uint32_t)emitter * 0x9E3779B9u) ^ ((uint32_t)burstIndex * 0x85EBCA6Bu) ^ BURST_SALT,
        (uint32_t)cursor->loop + (uint32_t)(cursor->firing >> 32u) * 0x9E3779B9u,
        (uint32_t)cursor->firing + (uint32_t)(cursor->loop >> 32u) * 0x85EBCA6Bu,
    });
    if (!(mvfxUnitFloat(words.y) < burst->probability))
    {
        return 0;
    }
    uint64_t span = (uint64_t)(burst->maxCount - burst->minCount) + 1;
    int64_t count = burst->minCount + (int64_t)(((uint64_t)words.x * span) >> 32u);
    // A quality level's rate scale, rounded to nearest, halves up.
    float scale = instance->rateScale;
    return scale == 1.0f ? count : (int64_t)floor((double)count * (double)scale + 0.5);
}

void mvfxOpenBirths(const mvfxInstance* instance, int32_t emitter, mvfxBirths* births)
{
    const mvfxEmitterState* state = &instance->emitters[emitter];
    births->instance = instance;
    births->emitter = emitter;
    births->rate = state->rate;
    births->distanceNext = state->distanceNext;
    for (int32_t b = 0; b < instance->effect->emitters[emitter].burstCount; ++b)
    {
        births->bursts[b] = state->bursts[b];
    }
}

int64_t mvfxNextBirths(mvfxBirths* births, int64_t limit, int64_t* tickOut)
{
    const mvfxInstance* instance = births->instance;
    const mvfxCompiledEmitter* emitter = &instance->effect->emitters[births->emitter];
    limit = limit < instance->stopTick ? limit : instance->stopTick;
    for (;;)
    {
        int64_t best = MVFX_NO_TICK;
        int32_t which = -1;
        for (int32_t b = 0; b < emitter->burstCount; ++b)
        {
            int64_t tick = PeekBurst(emitter, &emitter->bursts[b], &births->bursts[b], limit);
            if (tick < best)
            {
                best = tick;
                which = b;
            }
        }
        int64_t rateTick = PeekRate(emitter, instance->rateScale, &births->rate, limit);
        if (rateTick < best)
        {
            best = rateTick;
            which = -1;
        }
        int64_t distanceTick = PeekDistance(instance, births->emitter, births);
        if (distanceTick < best)
        {
            best = distanceTick;
            which = -2;
        }
        if (best > limit)
        {
            return 0;
        }
        *tickOut = best;
        if (which == -2)
        {
            births->distanceNext += 1;
            return 1;
        }
        if (which < 0)
        {
            births->rate.last = best - LoopStart(emitter, births->rate.loop);
            births->rate.next += 1;
            return 1;
        }
        mvfxBurstCursor* cursor = &births->bursts[which];
        int64_t count =
            BurstCount(instance, births->emitter, which, &emitter->bursts[which], cursor);
        cursor->firing += 1;
        if (count > 0)
        {
            return count;
        }
    }
}

void mvfxSaveBirths(mvfxInstance* instance, const mvfxBirths* births)
{
    mvfxEmitterState* state = &instance->emitters[births->emitter];
    state->rate = births->rate;
    state->distanceNext = births->distanceNext;
    for (int32_t b = 0; b < instance->effect->emitters[births->emitter].burstCount; ++b)
    {
        state->bursts[b] = births->bursts[b];
    }
}

void mvfxPlanDistance(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    state->distanceDelta =
        instance->stepDistance * (double)compiled->ratePerDistance * (double)instance->rateScale;
    state->distanceSkip = 0.0;
    state->distanceTake = 0;
    state->distanceNext = 0;
    double owed = floor(state->distanceCarry + state->distanceDelta);
    if (!(owed >= 1.0))
    {
        return;
    }
    double capacity = (double)instance->emitters[emitter].capacity;
    double take = owed < capacity ? owed : capacity;
    state->distanceTake = (uint64_t)take;
    state->distanceSkip = owed - take;
    if (state->distanceSkip > 0.0)
    {
        // At most 2^63 counted, so a jump's refusals never wrap a count.
        uint64_t skipped =
            state->distanceSkip < 0x1p63 ? (uint64_t)state->distanceSkip : (uint64_t)1 << 63u;
        state->owed = mvfxSaturatingAdd(state->owed, skipped);
        state->refused = mvfxSaturatingAdd(state->refused, skipped);
    }
}

void mvfxEndDistance(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    double total = state->distanceCarry + state->distanceDelta;
    // The fraction past the last birth; none after a jump too long for a
    // binary64 to hold one.
    double carry = total - floor(total);
    state->distanceCarry = total < 0x1p52 && carry >= 0.0 && carry < 1.0 ? carry : 0.0;
    state->distanceDelta = 0.0;
    state->distanceTake = 0;
    state->distanceNext = 0;
}

bool mvfxHasBirthsAhead(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    if (state->capacity == 0)
    {
        // Left out at the instance's quality level.
        return false;
    }
    if (state->hostCount > 0)
    {
        // The host's queue is born at the next step, stopped or not.
        return true;
    }
    if (instance->stopTick != INT64_MAX && instance->clock >= instance->stopTick)
    {
        return false;
    }
    if (compiled->loops > 0 && instance->clock >= LoopStart(compiled, (uint64_t)compiled->loops))
    {
        return false;
    }
    if (compiled->ratePerDistance > 0.0f || compiled->beam.enabled)
    {
        // It spawns whenever the instance moves, or strikes its beam again.
        return true;
    }
    if (compiled->eventSpawn.enabled)
    {
        // It spawns while its source may write events.
        int32_t source = compiled->eventSpawn.source;
        if (instance->emitters[source].count > 0 || mvfxHasBirthsAhead(instance, source))
        {
            return true;
        }
    }
    if (compiled->duration > 0)
    {
        // Every loop adds the rate's integral and starts the bursts again.
        bool rateSpawns =
            compiled->rate > 0.0f && compiled->loopIntegral > 0.0 && instance->rateScale > 0.0f;
        bool burstSpawns = false;
        for (int32_t b = 0; b < compiled->burstCount; ++b)
        {
            const mvfxCompiledBurst* burst = &compiled->bursts[b];
            burstSpawns = burstSpawns || (burst->maxCount > 0 && burst->probability > 0.0f &&
                                          burst->time < compiled->duration);
        }
        return rateSpawns || burstSpawns;
    }
    if (mvfxRateBirthTick(compiled, instance->rateScale, 0, state->rate.next) != MVFX_NO_TICK)
    {
        return true;
    }
    for (int32_t b = 0; b < compiled->burstCount; ++b)
    {
        const mvfxCompiledBurst* burst = &compiled->bursts[b];
        bool firesAgain = burst->cycles == 0 || state->bursts[b].firing < (uint64_t)burst->cycles;
        if (firesAgain && burst->maxCount > 0 && burst->probability > 0.0f)
        {
            return true;
        }
    }
    return false;
}
