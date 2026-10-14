// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "event.h"

#include "appearance.h"
#include "context.h"
#include "direction.h"
#include "instance_api.h"
#include "space.h"

void mvfxBeginEvents(mvfxInstance* instance, int32_t emitter)
{
    instance->emitters[emitter].eventCount = 0;
}

void mvfxRecordEvent(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    if ((compiled->events & (1u << fact->kind)) == 0)
    {
        return;
    }
    if (state->eventCount == compiled->eventCapacity)
    {
        state->eventsDropped = mvfxSaturatingAdd(state->eventsDropped, 1);
        return;
    }
    int32_t i = fact->particle;
    const uint32_t* serial = mvfxAttributeArray(instance, emitter, mvfx_attributeSerial);
    mvfxEvent* event = &state->events[state->eventCount++];
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
