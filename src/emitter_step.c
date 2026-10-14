// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Removing ended particles in place, and the births spawn.h gives in
// tick order, their values from birth.h.

#include "emitter_step.h"

#include "beam.h"
#include "birth.h"
#include "bounds.h"
#include "death_heap.h"
#include "event.h"
#include "event_spawn.h"
#include "host_module.h"
#include "integrate.h"
#include "scene_query.h"
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
    const mvfxAttributePlan* plan = &instance->effect->emitters[emitter].plan;
    for (int32_t i = kept; i < state->count; ++i)
    {
        int64_t birth = (int64_t)(((uint64_t)birthHigh[i] << 32u) | birthLow[i]);
        if (AgeAt(instance->clock, birth) * inverseLife[i] >= 1.0f)
        {
            // Its death at its tick, its place its last, from the step's start.
            mvfxRecordEvent(instance, emitter,
                            &(mvfxEventFact){.kind = mvfx_eventDeath,
                                             .particle = i,
                                             .tick = mvfxDeathTick(birth, inverseLife[i]),
                                             .placeTick = instance->stepStart});
            continue;
        }
        for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if (mvfxHasAttribute(plan, a))
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
    int64_t count =
        instance->emitters[emitter].hostCount + instance->emitters[emitter].eventQueueCount;
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

// Appends the event births of the queue from next up to a tick, with room
// for all: those that live past the step.
static void AppendEventBirthsBy(mvfxInstance* instance, int32_t emitter, int64_t limit,
                                int32_t* next)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    for (; *next < state->eventQueueCount && state->eventQueue[*next].tick <= limit; ++*next)
    {
        uint64_t k = state->owed;
        state->owed = mvfxSaturatingAdd(state->owed, 1);
        int64_t tick = state->eventQueue[*next].tick;
        float inverseLife = mvfxInverseLifeOf(instance, emitter, k, tick);
        if (AgeAt(instance->clock, tick) * inverseLife < 1.0f)
        {
            mvfxBirth birth = {k, tick, 0, 1, -1, *next + 1};
            mvfxAppendBirth(instance, emitter, &birth, inverseLife);
        }
    }
}

// The most strikes of a beam a step works through: past them the beam is
// struck at the step's end, so a lifetime of a tick costs no more than
// this in a step of any length.
#define MAX_STRIKES_A_STEP 64

// A beam's strikes by the step's end: each at the tick the last one's
// points die (its first at the emitter's start), while the instance has
// not stopped; a strike's points all take its first's lifetime, and one
// that dies within the step is skipped, its serials still owed. Past
// MAX_STRIKES_A_STEP strikes the next is struck at the step's end.
static void StrikeBeam(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!compiled->beam.enabled)
    {
        return;
    }
    int32_t points = compiled->beam.segments + 1;
    for (int32_t strikes = 0; state->count == 0 && state->beamStrike <= instance->clock &&
                              state->beamStrike < instance->stopTick;
         ++strikes)
    {
        if (strikes == MAX_STRIKES_A_STEP)
        {
            state->beamStrike = instance->clock;
        }
        int64_t tick = state->beamStrike;
        uint64_t first = state->owed;
        float inverseLife = mvfxInverseLifeOf(instance, emitter, first, tick);
        int64_t death = mvfxDeathTick(tick, inverseLife);
        state->beamStrike = death;
        if (death <= instance->clock)
        {
            state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)points);
            continue;
        }
        for (int32_t i = 0; i < points; ++i)
        {
            uint64_t k = state->owed;
            state->owed = mvfxSaturatingAdd(state->owed, 1);
            mvfxBirth birth = {k, tick, i, points, -1, 0};
            mvfxAppendBirth(instance, emitter, &birth, inverseLife);
        }
    }
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
            mvfxBirth birth = {k, start, h, state->hostCount, h, 0};
            mvfxAppendBirth(instance, emitter, &birth, inverseLife);
        }
    }
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t tick = 0;
    int64_t count = 0;
    int32_t next = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        // Events' births at a tick come before the emitter's own.
        AppendEventBirthsBy(instance, emitter, tick, &next);
        for (int64_t i = 0; i < count; ++i)
        {
            uint64_t k = state->owed;
            state->owed = mvfxSaturatingAdd(state->owed, 1);
            float inverseLife = mvfxInverseLifeOf(instance, emitter, k, tick);
            if (AgeAt(instance->clock, tick) * inverseLife < 1.0f)
            {
                mvfxBirth birth = {k, tick, i, count, -1, 0};
                mvfxAppendBirth(instance, emitter, &birth, inverseLife);
            }
        }
    }
    AppendEventBirthsBy(instance, emitter, instance->clock, &next);
    mvfxSaveBirths(instance, &births);
}

// Replays the event births of the queue from next up to a tick in a full
// emitter: each tick's deaths first, refused when it is full.
static void AcceptEventBirthsBy(mvfxInstance* instance, int32_t emitter, mvfxDeathHeap* heap,
                                int64_t limit, int32_t* next, int32_t* accepted)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    for (; *next < state->eventQueueCount && state->eventQueue[*next].tick <= limit; ++*next)
    {
        int64_t tick = state->eventQueue[*next].tick;
        (void)mvfxPopDeathsBy(heap, tick);
        uint64_t k = state->owed;
        state->owed = mvfxSaturatingAdd(state->owed, 1);
        if (heap->count >= compiled->capacity)
        {
            state->refused = mvfxSaturatingAdd(state->refused, 1);
            continue;
        }
        int64_t death = mvfxDeathTick(tick, mvfxInverseLifeOf(instance, emitter, k, tick));
        mvfxPushDeath(heap, death);
        if (death > instance->clock)
        {
            instance->acceptedScratch[(*accepted)++] = (mvfxBirth){k, tick, 0, 1, -1, *next + 1};
        }
    }
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
            instance->acceptedScratch[accepted++] =
                (mvfxBirth){k, start, h, state->hostCount, h, 0};
        }
    }
    mvfxBirths births;
    mvfxOpenBirths(instance, emitter, &births);
    int64_t tick = 0;
    int64_t count = 0;
    int32_t next = 0;
    while ((count = mvfxNextBirths(&births, instance->clock, &tick)) > 0)
    {
        AcceptEventBirthsBy(instance, emitter, &heap, tick, &next, &accepted);
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
                instance->acceptedScratch[accepted] = (mvfxBirth){k, tick, i, count, -1, 0};
                ++accepted;
            }
        }
        state->owed = mvfxSaturatingAdd(state->owed, (uint64_t)count);
        state->refused = mvfxSaturatingAdd(state->refused, (uint64_t)(count - taken));
    }
    AcceptEventBirthsBy(instance, emitter, &heap, instance->clock, &next, &accepted);
    mvfxSaveBirths(instance, &births);
    RemoveEnded(instance, emitter);
    mvfxRunHostModules(instance, emitter, mvfx_stageBeforeMotion);
    mvfxIntegrateEmitter(instance, emitter);
    for (int32_t i = 0; i < accepted; ++i)
    {
        const mvfxBirth* birth = &instance->acceptedScratch[i];
        mvfxAppendBirth(instance, emitter, birth,
                        mvfxInverseLifeOf(instance, emitter, birth->serial, birth->tick));
    }
}

// Takes out the particles the step killed, keeping the others' order,
// and counts them.
static void RemoveKilled(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    const uint8_t* killed = instance->killScratch;
    const mvfxAttributePlan* plan = &instance->effect->emitters[emitter].plan;
    int32_t kept = 0;
    for (int32_t i = 0; i < state->count; ++i)
    {
        if (killed[i] != 0)
        {
            mvfxRecordEvent(instance, emitter,
                            &(mvfxEventFact){.kind = mvfx_eventDeath,
                                             .killed = true,
                                             .particle = i,
                                             .tick = instance->clock,
                                             .placeTick = instance->clock});
            continue;
        }
        for (int32_t a = 0; kept != i && a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if (mvfxHasAttribute(plan, a))
            {
                uint32_t* array = mvfxAttributeArray(instance, emitter, a);
                array[kept] = array[i];
            }
        }
        ++kept;
    }
    state->killed = mvfxSaturatingAdd(state->killed, (uint64_t)(state->count - kept));
    state->count = kept;
}

void mvfxStepEmitter(mvfxInstance* instance, int32_t emitter)
{
    mvfxBeginEvents(instance, emitter);
    mvfxPlanDistance(instance, emitter);
    mvfxResolveEmitter(instance, emitter);
    mvfxBuildEventBirths(instance, emitter);
    // The particles alive at the step's start move once the ended ones are
    // gone and before the births, which move from their ticks as they are
    // appended; the killed are taken out after all of them.
    if (BirthsFit(instance, emitter))
    {
        RemoveEnded(instance, emitter);
        mvfxRunHostModules(instance, emitter, mvfx_stageBeforeMotion);
        mvfxIntegrateEmitter(instance, emitter);
        BirthsWithRoom(instance, emitter);
    }
    else
    {
        BirthsInTickOrder(instance, emitter);
    }
    mvfxRunHostModules(instance, emitter, mvfx_stageAfterMotion);
    RemoveKilled(instance, emitter);
    StrikeBeam(instance, emitter);
    mvfxPlaceBeam(instance, emitter);
    mvfxWriteSceneQueries(instance, emitter);
    mvfxMeasureBounds(instance, emitter);
    mvfxEndDistance(instance, emitter);
    instance->emitters[emitter].hostCount = 0;
}
