// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Removing ended particles in place, and the births spawn.h gives in
// tick order, their values from birth.h.

#include "emitter_step.h"

#include "birth.h"
#include "death_heap.h"
#include "spawn.h"

#include <math.h>

// The seconds in a tick, exact.
#define SECONDS_PER_TICK 0x1p-20

// A particle's age in seconds from its birth tick.
static float AgeAt(int64_t clock, int64_t birth)
{
    return (float)((double)(clock - birth) * SECONDS_PER_TICK);
}

// Removes the particles whose life has ended, moving the rest down in
// order.
static void RemoveEnded(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    const uint32_t* birthLow = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthLow);
    const uint32_t* birthHigh = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthHigh);
    const float* inverseLife = mvfxAttributeArray(instance, emitter, mvfx_attributeInverseLife);
    int32_t kept = 0;
    // The first ended particle: before it nothing moves.
    while (kept < state->count)
    {
        int64_t birth = (int64_t)(((uint64_t)birthHigh[kept] << 32u) | birthLow[kept]);
        if (AgeAt(instance->clock, birth) * inverseLife[kept] >= 1.0f)
        {
            break;
        }
        ++kept;
    }
    uint64_t present = instance->effect->emitters[emitter].plan.present;
    for (int32_t i = kept; i < state->count; ++i)
    {
        int64_t birth = (int64_t)(((uint64_t)birthHigh[i] << 32u) | birthLow[i]);
        if (AgeAt(instance->clock, birth) * inverseLife[i] >= 1.0f)
        {
            continue;
        }
        for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if ((present & ((uint64_t)1 << a)) != 0)
            {
                uint32_t* array = mvfxAttributeArray(instance, emitter, a);
                array[kept] = array[i];
            }
        }
        ++kept;
    }
    state->count = kept;
}

int64_t mvfxDeathTick(int64_t birth, float inverseLife)
{
    double life = 1.0 / (double)inverseLife;
    int64_t span = (int64_t)ceil(life * (double)MVFX_TICKS_PER_SECOND);
    int64_t tick = birth + (span > 1 ? span : 1);
    while (tick > birth + 1 && AgeAt(tick - 1, birth) * inverseLife >= 1.0f)
    {
        --tick;
    }
    while (AgeAt(tick, birth) * inverseLife < 1.0f)
    {
        ++tick;
    }
    return tick;
}

// Whether the step's births fit beside the live particles: they are
// counted on a copy of the cursors, up to the room there is.
static bool BirthsFit(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    int64_t room = compiled->capacity - instance->emitters[emitter].count;
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t tick = 0;
    int64_t count = instance->emitters[emitter].hostCount;
    while (count <= room)
    {
        int64_t next = mvfxNextBirths(&births, instance->clock, &tick);
        if (next == 0)
        {
            return true;
        }
        count += next;
    }
    return false;
}

// The births of a step whose emitter cannot fill: none is refused, so the
// ones that live past the step are appended in order.
static void BirthsWithRoom(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    for (int32_t h = 0; h < state->hostCount; ++h)
    {
        uint64_t k = state->owed;
        state->owed = mvfxSaturatingAdd(state->owed, 1);
        int64_t start = instance->stepStart;
        float inverseLife = mvfxInverseLifeOf(instance, emitter, k, start);
        if (AgeAt(instance->clock, start) * inverseLife < 1.0f)
        {
            mvfxBirth birth = {k, start, h, state->hostCount, h};
            mvfxAppendBirth(instance, emitter, &birth, inverseLife);
        }
    }
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t tick = 0;
    int64_t count = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        for (int64_t i = 0; i < count; ++i)
        {
            uint64_t k = state->owed;
            state->owed = mvfxSaturatingAdd(state->owed, 1);
            float inverseLife = mvfxInverseLifeOf(instance, emitter, k, tick);
            if (AgeAt(instance->clock, tick) * inverseLife < 1.0f)
            {
                mvfxBirth birth = {k, tick, i, count, -1};
                mvfxAppendBirth(instance, emitter, &birth, inverseLife);
            }
        }
    }
    mvfxSaveBirths(instance, &births);
}

// The births of a step whose emitter may fill, replayed in tick order as
// if it were stepped one tick at a time (each tick's deaths before its
// births): births that find the emitter full are refused, the last of a
// burst's first. The particles alive at the step's start are those before
// it, and stay until their death ticks.
static void BirthsInTickOrder(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    const uint32_t* birthLow = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthLow);
    const uint32_t* birthHigh = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthHigh);
    const float* inverseLife = mvfxAttributeArray(instance, emitter, mvfx_attributeInverseLife);
    mvfxDeathHeap heap = {instance->deathScratch, state->count};
    for (int32_t i = 0; i < state->count; ++i)
    {
        int64_t birth = (int64_t)(((uint64_t)birthHigh[i] << 32u) | birthLow[i]);
        heap.ticks[i] = mvfxDeathTick(birth, inverseLife[i]);
    }
    mvfxBuildDeathHeap(&heap);
    int32_t accepted = 0;
    // The host's particles first, at the step's start, where nothing has
    // yet died.
    int64_t start = instance->stepStart;
    for (int32_t h = 0; h < state->hostCount; ++h)
    {
        uint64_t k = state->owed;
        state->owed = mvfxSaturatingAdd(state->owed, 1);
        if (heap.count >= compiled->capacity)
        {
            state->refused = mvfxSaturatingAdd(state->refused, 1);
            continue;
        }
        int64_t death = mvfxDeathTick(start, mvfxInverseLifeOf(instance, emitter, k, start));
        mvfxPushDeath(&heap, death);
        if (death > instance->clock)
        {
            instance->acceptedScratch[accepted++] = (mvfxBirth){k, start, h, state->hostCount, h};
        }
    }
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t tick = 0;
    int64_t count = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        (void)mvfxPopDeathsBy(&heap, tick);
        int64_t room = compiled->capacity - heap.count;
        int64_t taken = count < room ? count : room;
        for (int64_t i = 0; i < taken; ++i)
        {
            uint64_t k = state->owed + (uint64_t)i;
            int64_t death = mvfxDeathTick(tick, mvfxInverseLifeOf(instance, emitter, k, tick));
            mvfxPushDeath(&heap, death);
            if (death > instance->clock)
            {
                instance->acceptedScratch[accepted] = (mvfxBirth){k, tick, i, count, -1};
                ++accepted;
            }
        }
        state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)count);
        state->refused = mvfxSaturatingAdd(state->refused, (uint64_t)(count - taken));
    }
    mvfxSaveBirths(instance, &births);
    RemoveEnded(instance, emitter);
    for (int32_t i = 0; i < accepted; ++i)
    {
        const mvfxBirth* birth = &instance->acceptedScratch[i];
        mvfxAppendBirth(instance, emitter, birth,
                        mvfxInverseLifeOf(instance, emitter, birth->serial, birth->tick));
    }
}

void mvfxStepEmitter(mvfxInstance* instance, int32_t emitter)
{
    mvfxPlanDistance(instance, emitter);
    if (BirthsFit(instance, emitter))
    {
        RemoveEnded(instance, emitter);
        BirthsWithRoom(instance, emitter);
    }
    else
    {
        BirthsInTickOrder(instance, emitter);
    }
    mvfxEndDistance(instance, emitter);
    instance->emitters[emitter].hostCount = 0;
}
