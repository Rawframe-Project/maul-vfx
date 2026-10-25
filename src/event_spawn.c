// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "event_spawn.h"

#include "random.h"
#include "space.h"

#include "maul-vfx/modules.h"

// The salts of an event's count and of its births' keys.
#define EVENT_SALT 0x45564E54u
#define KEY_SALT   0x4B455953u

// Whether a tick lies where the emitter spawns: from its delay, within its
// loops, not past a stop.
static bool IsSpawning(const mvfxInstance* instance, const mvfxCompiledEmitter* emitter,
                       int64_t tick)
{
    int64_t end = emitter->delay + (int64_t)emitter->loops * emitter->duration;
    return tick >= emitter->delay && tick <= instance->stopTick &&
           (emitter->loops == 0 || tick <= end);
}

uint64_t mvfxEventBirthKey(uint32_t source, int64_t tick, int32_t index)
{
    mvfxWords4 words = mvfxPcg4d((mvfxWords4){
        (uint32_t)(uint64_t)tick, (uint32_t)((uint64_t)tick >> 32u), (uint32_t)index, KEY_SALT});
    return (uint64_t)source | (uint64_t)(0x80000000u | (words.x >> 1u)) << 32u;
}

// The particles an event spawns, drawn from its serial and tick.
static int32_t CountOf(const mvfxInstance* instance, int32_t emitter, const mvfxEventSpawn* spawn,
                       const mvfxEvent* event)
{
    uint32_t span = (uint32_t)(spawn->maxCount - spawn->minCount) + 1u;
    mvfxWords4 words =
        mvfxPcg4d((mvfxWords4){instance->seed, (uint32_t)emitter,
                               EVENT_SALT ^ (uint32_t)(uint64_t)event->tick, event->serial});
    return spawn->minCount + (int32_t)(((uint64_t)words.x * span) >> 32u);
}

// x held to [-bound, bound].
static float Hold(float x, float bound)
{
    return x < -bound ? -bound : (x > bound ? bound : x);
}

// A birth of an event, its place and inherited velocity in the emitter's
// terms at the event's tick: a world-space emitter's place in the world,
// a local one's in the instance's space.
static mvfxHostBirth BirthOf(const mvfxInstance* instance, int32_t emitter,
                             const mvfxEventSpawn* spawn, const mvfxEvent* event)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxTransform transform;
    mvfxTransformAt(instance, event->tick, &transform);
    mvfxSpaceMap map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &transform);
    mvfxHostBirth birth = {
        .given = mvfx_givenAddedVelocity, .tick = event->tick, .source = event->serial};
    float inherited[3] = {event->velocity[0] * spawn->inheritVelocity,
                          event->velocity[1] * spawn->inheritVelocity,
                          event->velocity[2] * spawn->inheritVelocity};
    mvfxMapVector(&map, inherited, map.sigma, birth.velocity);
    if (compiled->space == mvfx_spaceWorld)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            birth.position[k] = event->place[k];
        }
    }
    else
    {
        float local[3];
        mvfxMapPointDouble(&map, event->place, local);
        for (int32_t k = 0; k < 3; ++k)
        {
            birth.position[k] = (double)local[k];
        }
    }
    // What it inherits held to what its emitter's particles may store: the
    // source's colour and size, as shown, may be past it.
    const mvfxStoredCaps* caps = &compiled->overLife.caps;
    if (spawn->inheritColor)
    {
        birth.given |= mvfx_givenColor;
        for (int32_t k = 0; k < 4; ++k)
        {
            birth.color[k] = Hold(event->color[k], caps->color[k]);
        }
    }
    if (spawn->inheritSize)
    {
        birth.given |= mvfx_givenSize;
        for (int32_t k = 0; k < 3; ++k)
        {
            birth.size[k] = Hold(event->size[k], caps->size[k]);
        }
    }
    return birth;
}

static bool Before(const mvfxHostBirth* a, const mvfxHostBirth* b)
{
    return a->tick < b->tick || (a->tick == b->tick && a->order < b->order);
}

static void SiftDown(mvfxHostBirth* births, int32_t root, int32_t count)
{
    for (;;)
    {
        int32_t child = 2 * root + 1;
        if (child >= count)
        {
            return;
        }
        if (child + 1 < count && Before(&births[child], &births[child + 1]))
        {
            child += 1;
        }
        if (!Before(&births[root], &births[child]))
        {
            return;
        }
        mvfxHostBirth swap = births[root];
        births[root] = births[child];
        births[child] = swap;
        root = child;
    }
}

// Adds a birth to a heap with its latest on top, keeping the earliest
// capacity of them: false when the birth or the one it displaces is left
// out.
static bool KeepEarliest(mvfxHostBirth* heap, int32_t* count, int32_t capacity,
                         const mvfxHostBirth* birth)
{
    if (*count < capacity)
    {
        int32_t at = (*count)++;
        heap[at] = *birth;
        while (at > 0 && Before(&heap[(at - 1) / 2], &heap[at]))
        {
            int32_t parent = (at - 1) / 2;
            mvfxHostBirth swap = heap[parent];
            heap[parent] = heap[at];
            heap[at] = swap;
            at = parent;
        }
        return true;
    }
    if (Before(birth, &heap[0]))
    {
        heap[0] = *birth;
        SiftDown(heap, 0, *count);
    }
    return false;
}

// Sorts a heap with its latest on top by tick, then by the births' order
// as built (the keys are distinct, so the result is the one order there
// is).
static void SortByTick(mvfxHostBirth* births, int32_t count)
{
    for (int32_t end = count - 1; end > 0; --end)
    {
        mvfxHostBirth swap = births[0];
        births[0] = births[end];
        births[end] = swap;
        SiftDown(births, 0, end);
    }
}

void mvfxBuildEventBirths(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    state->eventQueueCount = 0;
    const mvfxEventSpawn* spawn = &compiled->eventSpawn;
    if (!spawn->enabled)
    {
        return;
    }
    const mvfxEmitterState* source = &instance->emitters[spawn->source];
    int32_t order = 0;
    // Its source's events of this step alone, after its earlier steps'.
    for (int32_t e = source->eventStepFirst; e < source->eventCount; ++e)
    {
        const mvfxEvent* event = &source->events[e];
        if (event->kind != spawn->kind || !IsSpawning(instance, compiled, event->tick))
        {
            continue;
        }
        int32_t count = CountOf(instance, emitter, spawn, event);
        for (int32_t c = 0; c < count; ++c)
        {
            mvfxHostBirth birth = BirthOf(instance, emitter, spawn, event);
            birth.order = order++;
            birth.key = mvfxEventBirthKey(event->serial, event->tick, c);
            if (!KeepEarliest(state->eventQueue, &state->eventQueueCount, spawn->capacity, &birth))
            {
                // More than a step takes from events: the latest refused, a
                // serial spent.
                state->owed = mvfxSaturatingAdd(state->owed, 1);
                state->refused = mvfxSaturatingAdd(state->refused, 1);
            }
        }
    }
    SortByTick(state->eventQueue, state->eventQueueCount);
}
