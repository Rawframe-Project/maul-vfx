// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Removing ended particles in place, and the births spawn.h gives in
// tick order. A particle's life is drawn from pcg4d at its serial (record
// mvfx-0001).

#include "emitter_step.h"

#include "death_heap.h"
#include "random.h"
#include "spawn.h"

#include <math.h>

// The salt that sets the life's draws apart from any other module's.
#define LIFE_SALT 0x4C494645u

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
    uint32_t present = instance->effect->emitters[emitter].plan.present;
    for (int32_t i = kept; i < state->count; ++i)
    {
        int64_t birth = (int64_t)(((uint64_t)birthHigh[i] << 32u) | birthLow[i]);
        if (AgeAt(instance->clock, birth) * inverseLife[i] >= 1.0f)
        {
            continue;
        }
        for (mvfxAttribute a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if ((present & (1u << a)) != 0)
            {
                uint32_t* array = mvfxAttributeArray(instance, emitter, a);
                array[kept] = array[i];
            }
        }
        ++kept;
    }
    state->count = kept;
}

// Appends particle k, born at birth with the given inverse life, at the
// emitter's origin.
static void Append(mvfxInstance* instance, int32_t emitter, uint64_t k, int64_t birth,
                   float inverseLife)
{
    int32_t at = instance->emitters[emitter].count;
    ((float*)mvfxAttributeArray(instance, emitter, mvfx_attributePositionX))[at] = 0.0f;
    ((float*)mvfxAttributeArray(instance, emitter, mvfx_attributePositionY))[at] = 0.0f;
    ((float*)mvfxAttributeArray(instance, emitter, mvfx_attributePositionZ))[at] = 0.0f;
    ((uint32_t*)mvfxAttributeArray(instance, emitter, mvfx_attributeBirthLow))[at] =
        (uint32_t)((uint64_t)birth & 0xFFFFFFFFu);
    ((uint32_t*)mvfxAttributeArray(instance, emitter, mvfx_attributeBirthHigh))[at] =
        (uint32_t)((uint64_t)birth >> 32u);
    ((float*)mvfxAttributeArray(instance, emitter, mvfx_attributeInverseLife))[at] = inverseLife;
    ((uint32_t*)mvfxAttributeArray(instance, emitter, mvfx_attributeSerial))[at] = (uint32_t)k;
    instance->emitters[emitter].count = at + 1;
}

// One over particle k's life, drawn from the emitter's range.
static float InverseLifeOf(const mvfxInstance* instance, int32_t emitter, uint64_t k)
{
    const mvfxRange* range = &instance->effect->emitters[emitter].lifetime;
    mvfxWords4 words = mvfxPcg4d((mvfxWords4){
        instance->seed,
        ((uint32_t)emitter * 0x9E3779B9u) ^ LIFE_SALT,
        (uint32_t)k,
        (uint32_t)(k >> 32u),
    });
    float life = range->min + (range->max - range->min) * mvfxUnitFloat(words.x);
    return 1.0f / life;
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
    int64_t count = 0;
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
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t tick = 0;
    int64_t count = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        for (int64_t i = 0; i < count; ++i)
        {
            uint64_t k = state->owed++;
            float inverseLife = InverseLifeOf(instance, emitter, k);
            if (AgeAt(instance->clock, tick) * inverseLife < 1.0f)
            {
                Append(instance, emitter, k, tick, inverseLife);
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
            int64_t death = mvfxDeathTick(tick, InverseLifeOf(instance, emitter, k));
            mvfxPushDeath(&heap, death);
            if (death > instance->clock)
            {
                instance->acceptedScratch[accepted] = k;
                instance->acceptedBirthScratch[accepted] = tick;
                ++accepted;
            }
        }
        state->owed += (uint64_t)count;
        state->refused += (uint64_t)(count - taken);
    }
    mvfxSaveBirths(instance, &births);
    RemoveEnded(instance, emitter);
    for (int32_t i = 0; i < accepted; ++i)
    {
        uint64_t k = instance->acceptedScratch[i];
        Append(instance, emitter, k, instance->acceptedBirthScratch[i],
               InverseLifeOf(instance, emitter, k));
    }
}

void mvfxStepEmitter(mvfxInstance* instance, int32_t emitter)
{
    if (BirthsFit(instance, emitter))
    {
        RemoveEnded(instance, emitter);
        BirthsWithRoom(instance, emitter);
    }
    else
    {
        BirthsInTickOrder(instance, emitter);
    }
}
