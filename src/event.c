// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "event.h"

#include "appearance.h"
#include "context.h"
#include "direction.h"
#include "instance_api.h"
#include "space.h"

#include <string.h>

void mvfxBeginEvents(mvfxInstance* instance, int32_t emitter)
{
    instance->emitters[emitter].eventCount = 0;
}

// Builds the event a fact records, its values taken now.
static void BuildEvent(const mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact,
                       mvfxEvent* event)
{
    int32_t i = fact->particle;
    const uint32_t* serial = mvfxAttributeArray(instance, emitter, mvfx_attributeSerial);
    *event = (mvfxEvent){
        .kind = fact->kind,
        .killed = fact->killed,
        .serial = serial[i],
        .tick = fact->tick,
    };
    mvfxTransform transform;
    mvfxTransformAt(instance, fact->placeTick, &transform);
    mvfxSpaceMap map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &transform);
    float place[3];
    float velocity[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        place[k] = fact->point != nullptr
                       ? fact->point[k]
                       : ((const float*)mvfxAttributeArray(
                             instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k)))[i];
        velocity[k] = ((const float*)mvfxAttributeArray(
            instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k)))[i];
    }
    mvfxUnmapPoint(&map, place, event->place);
    mvfxUnmapVector(&map, velocity, event->velocity);
    if (fact->normal != nullptr)
    {
        // A normal turns back, unscaled.
        mvfxUnmapVector(&map, fact->normal, event->normal);
        (void)mvfxNormalize(event->normal);
    }
    mvfxColorOf(instance, emitter, i, event->color);
    mvfxSizeOf(instance, emitter, i, event->size);
}

// Whether an emitter writes events of a kind.
static bool Writes(const mvfxInstance* instance, int32_t emitter, mvfxEventKind kind)
{
    return (instance->effect->emitters[emitter].events & (1u << kind)) != 0;
}

// Appends an event, or counts it dropped when the buffer is full.
static void Append(mvfxInstance* instance, int32_t emitter, const mvfxEvent* event)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (state->eventCount == instance->effect->emitters[emitter].eventCapacity)
    {
        state->eventsDropped = mvfxSaturatingAdd(state->eventsDropped, 1);
        return;
    }
    state->events[state->eventCount++] = *event;
}

void mvfxRecordEvent(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact)
{
    if (!Writes(instance, emitter, fact->kind))
    {
        return;
    }
    mvfxEvent event;
    BuildEvent(instance, emitter, fact, &event);
    Append(instance, emitter, &event);
}

void mvfxBeginHeldContacts(mvfxInstance* instance, int32_t emitter, int32_t count)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    state->holdingContacts = mvfxHoldsContacts(&instance->effect->emitters[emitter]);
    if (state->holdingContacts)
    {
        memset(state->unitContactHeld, 0, (size_t)count);
    }
}

void mvfxHoldContact(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    int32_t i = fact->particle;
    if (state->unitContactHeld[i] == 0)
    {
        BuildEvent(instance, emitter, fact, &state->unitContacts[i]);
        state->unitContactHeld[i] = 1;
    }
}

void mvfxEndHeldContacts(mvfxInstance* instance, int32_t emitter, int32_t count)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!state->holdingContacts)
    {
        return;
    }
    state->holdingContacts = false;
    for (int32_t i = 0; i < count; ++i)
    {
        if (state->unitContactHeld[i] != 0)
        {
            Append(instance, emitter, &state->unitContacts[i]);
        }
    }
}

mvfxResult mvfxInstance_GetEvents(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                  const mvfxEvent** eventsOut, int32_t* countOut)
{
    if (context == nullptr || eventsOut == nullptr || countOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *eventsOut = nullptr;
    *countOut = 0;
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    const mvfxEmitterState* state = &live->emitters[emitter];
    *eventsOut = state->eventCount > 0 ? state->events : nullptr;
    *countOut = state->eventCount;
    return mvfx_success;
}
