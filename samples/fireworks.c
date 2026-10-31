// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fireworks: rockets launched in bursts rise and die, each death an event
// that bursts into forty sparks which keep some of the rocket's velocity
// and fall, coloured from white to red as they age. The host reads the
// rockets' deaths and the sparks' births after every step. It prints a
// line a second and returns 0 when every rocket that died burst into
// exactly its forty sparks and none was refused.

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SPARKS_EACH 40

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    // One or two rockets every half second.
    static const mvfxBurst launches = {
        .minCount = 1, .maxCount = 2, .cycles = 0, .interval = 0.5f, .probability = 1.0f};
    static const mvfxColorKey colors[3] = {
        {0.0f, 1.0f, 1.0f, 1.0f}, {0.3f, 1.0f, 0.8f, 0.2f}, {1.0f, 0.6f, 0.05f, 0.0f}};
    static const mvfxAlphaKey alphas[2] = {{0.0f, 1.0f}, {1.0f, 0.0f}};

    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    mvfxEmitterDef* rockets = &emitters[0];
    rockets->capacity = 16;
    rockets->rate = 0.0f;
    rockets->bursts = &launches;
    rockets->burstCount = 1;
    rockets->lifetime = mvfxRangeSource(1.1f, 1.3f);
    rockets->velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                       .direction = {0.0f, 1.0f, 0.0f},
                                       .coneAngle = 0.3f,
                                       .speed = {12.0f, 14.0f}};
    rockets->motion.gravity[1] = -9.8f;
    rockets->events = mvfx_eventMaskDeath;
    rockets->eventCapacity = 4;

    // Sparks: forty from each rocket's death, flying out every way at 3 to
    // 5 a second plus a third of the rocket's velocity, slowed by air.
    mvfxEmitterDef* sparks = &emitters[1];
    sparks->capacity = 512;
    sparks->rate = 0.0f;
    sparks->lifetime = mvfxRangeSource(1.0f, 1.5f);
    sparks->velocity.speed = (mvfxRange){3.0f, 5.0f};
    sparks->motion.gravity[1] = -9.8f;
    sparks->motion.drag = 0.8f;
    sparks->eventSpawn = (mvfxEventSpawn){.enable = true,
                                          .source = 0,
                                          .kind = mvfx_eventDeath,
                                          .minCount = SPARKS_EACH,
                                          .maxCount = SPARKS_EACH,
                                          .inheritVelocity = 0.3f,
                                          .capacity = 256};
    sparks->overLife.color.enable = true;
    sparks->overLife.color.gradient = (mvfxGradient){
        .colorKeys = colors, .colorKeyCount = 3, .alphaKeys = alphas, .alphaKeyCount = 2};
    // Their births written as events too, for the host to count.
    sparks->events = mvfx_eventMaskBirth;
    sparks->eventCapacity = 256;

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 2;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect, NULL) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

int main(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("no context\n");
        return 1;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    int64_t deaths = 0;
    int64_t births = 0;
    double highest = 0.0;
    // Six seconds at 60 steps a second; each step's events read after it.
    for (int32_t step = 1; ok && step <= 6 * 60; ++step)
    {
        const mvfxEvent* events = NULL;
        int32_t count = 0;
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
             mvfxInstance_GetEvents(context, instance, 0, &events, &count) == mvfx_success;
        for (int32_t i = 0; ok && i < count; ++i)
        {
            highest = events[i].place[1] > highest ? events[i].place[1] : highest;
        }
        deaths += count;
        ok = ok && mvfxInstance_GetEvents(context, instance, 1, &events, &count) == mvfx_success;
        births += count;
        if (ok && step % 60 == 0)
        {
            mvfxEmitterStats rockets;
            mvfxEmitterStats sparks;
            ok = mvfxInstance_GetEmitterStats(context, instance, 0, &rockets) == mvfx_success &&
                 mvfxInstance_GetEmitterStats(context, instance, 1, &sparks) == mvfx_success &&
                 rockets.eventsDropped == 0 && sparks.eventsDropped == 0 &&
                 sparks.birthsDropped == 0;
            printf("%d s: %d rockets and %d sparks alive; %lld rockets burst so far, the "
                   "highest at %.1f, into %lld sparks\n",
                   step / 60, rockets.live, sparks.live, (long long)deaths, highest,
                   (long long)births);
        }
    }
    mvfxDestroyContext(context);
    ok = ok && deaths >= 10 && births == SPARKS_EACH * deaths;
    printf(ok ? "every rocket burst into its sparks\n" : "the fireworks failed\n");
    return ok ? 0 : 1;
}
