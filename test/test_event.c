// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Events: births at their ticks and places, deaths by age at their death
// ticks from their last places, deaths by kills at the step's end, and
// collisions at their first contacts with the surface's normal; the order
// a step meets them; the mask and the capacity with the dropped counted;
// a local emitter's events in the world with its colour and size; a
// restart and a finished instance's step clearing them; and the
// refusals.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

// An emitter of what the host spawns, writing every kind of event.
static mvfxEmitterDef Telling(float life)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 16;
    emitter.lifetime = mvfxRangeSource(life, life);
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath;
    emitter.eventCapacity = 16;
    return emitter;
}

static void Place(Scene* scene, double x, double y, double z, float vx, float vy, float vz)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, vy, vz};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
}

static bool Near(double a, double b, double within)
{
    return fabs(a - b) <= within;
}

static void Step(Scene* scene, int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(scene->context, ticks) == mvfx_success, "step");
}

static int32_t Events(Scene* scene, const mvfxEvent** events)
{
    int32_t count = -1;
    CHECK(mvfxInstance_GetEvents(scene->context, scene->instance, 0, events, &count) ==
              mvfx_success,
          "events");
    return count;
}

static void TestBirthsAndDeaths(void)
{
    mvfxEmitterDef emitter = Telling(0.5f);
    Scene scene = NewScene(&emitter);
    Place(&scene, 1, 2, 3, 0, 1, 0);
    Place(&scene, 4, 5, 6, 0, 0, 0);
    Step(&scene, 1);
    const mvfxEvent* events = NULL;
    CHECK(Events(&scene, &events) == 2, "two births");
    CHECK(events[0].kind == mvfx_eventBirth && events[0].serial == 0 && events[0].tick == 0 &&
              events[0].place[0] == 1.0 && events[0].place[2] == 3.0 &&
              events[0].velocity[1] == 1.0f && events[1].serial == 1 && events[1].place[1] == 5.0 &&
              events[0].color[0] == 1.0f && events[0].size[2] == 1.0f &&
              events[0].normal[1] == 0.0f,
          "their serials, ticks, places and velocities");
    // Half a second's life: each ends at tick TPS / 2, where it last was.
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Events(&scene, &events) == 2, "two deaths");
    CHECK(events[0].kind == mvfx_eventDeath && !events[0].killed &&
              events[0].tick == MVFX_TICKS_PER_SECOND / 2 && events[0].serial == 0 &&
              Near(events[0].place[1], 2.0, 1e-5) && events[1].serial == 1,
          "at their death ticks and last places");
    mvfxDestroyContext(scene.context);

    // In one step a death by age comes before a birth.
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Step(&scene, 1);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    // To the first's death tick, before the second's.
    Step(&scene, MVFX_TICKS_PER_SECOND / 2 - 1);
    CHECK(Events(&scene, &events) == 2 && events[0].kind == mvfx_eventDeath &&
              events[1].kind == mvfx_eventBirth && events[1].serial == 1,
          "a death before a birth");
    mvfxDestroyContext(scene.context);
}

static void TestKillsAndCollisions(void)
{
    // Out of a sphere of radius 1 at 2 a second: killed at the step's end,
    // where it ended.
    mvfxKillRule rule = {.kind = mvfx_killOutsideSphere, .radius = 1.0f};
    mvfxEmitterDef emitter = Telling(10.0f);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 2, 0, 0);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    const mvfxEvent* events = NULL;
    CHECK(Events(&scene, &events) == 1 && events[0].kind == mvfx_eventDeath && events[0].killed &&
              events[0].tick == MVFX_TICKS_PER_SECOND + 1 && Near(events[0].place[0], 2.0, 1e-4),
          "killed at the step's end");
    mvfxDestroyContext(scene.context);

    // Onto a floor from y = 1 at 2: a collision halfway, at the floor, its
    // normal up, its velocity after the bounce.
    static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    emitter = Telling(10.0f);
    emitter.events = mvfx_eventMaskCollision;
    emitter.collision = (mvfxCollision){.enabled = true, .restitution = 1.0f};
    emitter.colliders = &floor;
    emitter.colliderCount = 1;
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -2, 0);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Events(&scene, &events) == 1 && events[0].kind == mvfx_eventCollision &&
              Near((double)events[0].tick, MVFX_TICKS_PER_SECOND / 2.0, 2.0) &&
              Near(events[0].place[1], 0.0, 1e-5) && events[0].normal[1] == 1.0f &&
              Near((double)events[0].velocity[1], 2.0, 1e-5),
          "a collision at its contact");
    mvfxDestroyContext(scene.context);
}

static void TestMaskAndCapacity(void)
{
    // Deaths only: births not written. Two a step: three of five dropped.
    mvfxEmitterDef emitter = Telling(10.0f);
    emitter.events = mvfx_eventMaskDeath;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Step(&scene, 1);
    const mvfxEvent* events = NULL;
    CHECK(Events(&scene, &events) == 0 && events == NULL, "births not asked");
    mvfxDestroyContext(scene.context);

    emitter = Telling(10.0f);
    emitter.eventCapacity = 2;
    scene = NewScene(&emitter);
    for (int32_t i = 0; i < 5; ++i)
    {
        Place(&scene, i, 0, 0, 0, 0, 0);
    }
    Step(&scene, 1);
    mvfxEmitterStats stats = {0};
    CHECK(Events(&scene, &events) == 2 && events[1].serial == 1 &&
              mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) ==
                  mvfx_success &&
              stats.eventsDropped == 3,
          "two kept, three dropped");
    // The next step writes its own, none here.
    Step(&scene, 1);
    CHECK(Events(&scene, &events) == 0, "the next step's own");
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Step(&scene, 1);
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(Events(&scene, &events) == 0 &&
              mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) ==
                  mvfx_success &&
              stats.eventsDropped == 0,
          "a restart clears them");
    mvfxDestroyContext(scene.context);

    // A finished instance's step clears the deaths it ended with.
    emitter = Telling(0.5f);
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Step(&scene, 1);
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "stopped");
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Events(&scene, &events) == 1, "its last death");
    Step(&scene, 1);
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance) && Events(&scene, &events) == 0,
          "finished, cleared");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // A local emitter under an instance at (100, 0, 0), turned a quarter
    // about Z, at scale 2, with a colour and a size: a birth at its
    // (1, 0, 0) moving (1, 0, 0) is at the world's (100, 2, 0) moving
    // (0, 2, 0).
    mvfxEmitterDef emitter = Telling(10.0f);
    emitter.space = mvfx_spaceLocal;
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                       .min = {0.25f, 0.5f, 0.75f, 1.0f},
                                       .max = {0.25f, 0.5f, 0.75f, 1.0f}};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {3, 3, 3, 0}, .max = {3, 3, 3, 0}, .uniform = true};
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 1, 0, 0, 1, 0, 0);
    Step(&scene, 1);
    const mvfxEvent* events = NULL;
    CHECK(Events(&scene, &events) == 1 && Near(events[0].place[0], 100.0, 1e-5) &&
              Near(events[0].place[1], 2.0, 1e-5) &&
              Near((double)events[0].velocity[1], 2.0, 1e-5) &&
              Near((double)events[0].velocity[0], 0.0, 1e-5),
          "in the world");
    CHECK(events[0].color[0] == 0.25f && events[0].color[2] == 0.75f && events[0].size[1] == 3.0f,
          "its colour and size");
    mvfxDestroyContext(scene.context);
}

static void TestMovingInstance(void)
{
    // A local emitter's instance moving from the origin to (10, 0, 0) over
    // each step: a birth at the step's start is placed where the instance
    // was then; a death by age, where the particle last was, at that
    // step's start.
    mvfxEmitterDef emitter = Telling(0.5f);
    emitter.space = mvfx_spaceLocal;
    Scene scene = NewScene(&emitter);
    static const double there[3] = {10.0, 0.0, 0.0};
    static const double back[3] = {0.0, 0.0, 0.0};
    static const float still[4] = {0, 0, 0, 1};
    Place(&scene, 0, 0, 0, 0, 0, 0);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, there, still, 1.0f, false) ==
              mvfx_success,
          "moving");
    Step(&scene, 1);
    const mvfxEvent* events = NULL;
    CHECK(Events(&scene, &events) == 1 && Near(events[0].place[0], 0.0, 1e-9),
          "a birth where the instance was at its tick");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, back, still, 1.0f, false) ==
              mvfx_success,
          "moving back");
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Events(&scene, &events) == 1 && events[0].kind == mvfx_eventDeath &&
              Near(events[0].place[0], 10.0, 1e-9),
          "a death where it last was");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Telling(10.0f);
    Scene scene = NewScene(&emitter);
    uint64_t misuse = mvfxGetContextMisuse(scene.context);
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    CHECK(mvfxInstance_GetEvents(scene.context, scene.instance, 0, NULL, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_GetEvents(scene.context, scene.instance, 0, &events, NULL) ==
                  mvfx_errorInvalid &&
              mvfxInstance_GetEvents(scene.context, scene.instance, 1, &events, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_GetEvents(scene.context, scene.instance, -1, &events, &count) ==
                  mvfx_errorInvalid,
          "refused");
    CHECK(mvfxGetContextMisuse(scene.context) == misuse + 4, "each counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_GetEvents(scene.context, stale, 0, &events, &count) == mvfx_errorStale &&
              mvfxInstance_GetEvents(NULL, scene.instance, 0, &events, &count) == mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(scene.context);

    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 4; ++field)
    {
        // Past a named bound: mvfx_errorLimit, data a correct program may
        // pass, so no misuse.
        mvfxResult want = field == 2 ? mvfx_errorLimit : mvfx_errorInvalid;
        mvfxEmitterDef def = Telling(10.0f);
        switch (field)
        {
        case 0:
            def.events = 8;
            break;
        case 1:
            def.eventCapacity = 0;
            break;
        case 2:
            def.eventCapacity = MVFX_MAX_EVENTS + 1;
            break;
        default:
            def.events = mvfx_eventMaskCollision;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &def;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == want, "refused");
    }
    // No events asked: the capacity is not read.
    mvfxEmitterDef quiet = Telling(10.0f);
    quiet.events = 0;
    quiet.eventCapacity = -5;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &quiet;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "none asked");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestBirthsAndDeaths();
    TestKillsAndCollisions();
    TestMaskAndCapacity();
    TestLocal();
    TestMovingInstance();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
