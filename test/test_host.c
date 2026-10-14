// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host spawns and prewarm: queued particles born first at the next step's
// start with their given values and drawn ones, in world and local space,
// the queue's limit, a full emitter, a stopped instance, restart, the
// refusals; prewarm spread over calls reaching the state of the same
// steps taken at once, and its refusals.

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxInstance* live;
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
    instanceDef.seed = 5;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

static const float* Array(const Scene* scene, mvfxAttribute attribute)
{
    return mvfxAttributeArray(scene->live, 0, attribute);
}

static mvfxEmitterStats Stats(const Scene* scene)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, 0, &stats) == mvfx_success,
          "stats");
    return stats;
}

static void TestWorldSpawns(void)
{
    // An instance at (10, 0, 0), turned a quarter about Z; the emitter's
    // own births go along X at 2 in its space, so along Y in the world.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.hostSpawnCapacity = 8;
    emitter.velocity =
        (mvfxVelocity){.mode = mvfx_directionFixed, .direction = {1, 0, 0}, .speed = {2.0f, 2.0f}};
    emitter.color =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 0, 1}, .max = {0, 0, 0, 1}};
    emitter.lifetime = mvfxRangeSource(5.0f, 5.0f);
    Scene scene = NewScene(&emitter);
    const double origin[3] = {10.0, 0.0, 0.0};
    const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 1.0f, true) ==
              mvfx_success,
          "placed");
    static const double places[6] = {100.0, 5.0, 0.0, -3.0, 2.0, 1.0};
    static const float velocities[6] = {0, 0, 9, 0, 0, 9};
    static const float colors[8] = {1, 0, 0, 1, 0, 1, 0, 1};
    mvfxSpawnRequest given = {
        .count = 2, .positions = places, .velocities = velocities, .colors = colors};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &given) == mvfx_success, "queued");
    mvfxSpawnRequest drawn = {.count = 1, .positions = places};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &drawn) == mvfx_success, "queued");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 16) == mvfx_success, "step");
    CHECK(scene.live->emitters[0].count == 7, "three of the host's and four of the rate's");
    const uint32_t* serial = mvfxAttributeArray(scene.live, 0, mvfx_attributeSerial);
    const uint32_t* birth = mvfxAttributeArray(scene.live, 0, mvfx_attributeBirthLow);
    const double* anchor = scene.live->emitters[0].anchor;
    bool first = serial[0] == 0 && serial[1] == 1 && serial[2] == 2 && birth[0] == 0 &&
                 birth[2] == 0 && birth[3] > 0;
    CHECK(first, "the host's first, at the step's start");
    bool placed = true;
    for (int32_t i = 0; i < 3; ++i)
    {
        const double* p = &places[3 * (i < 2 ? i : 0)];
        // Born at the step's start, they flew the whole step since.
        double h = 1.0 / 16.0;
        double x = (double)Array(&scene, mvfx_attributePositionX)[i] -
                   (double)Array(&scene, mvfx_attributeVelocityX)[i] * h;
        double y = (double)Array(&scene, mvfx_attributePositionY)[i] -
                   (double)Array(&scene, mvfx_attributeVelocityY)[i] * h;
        placed = placed && fabs(x + anchor[0] - p[0]) <= 1e-4 && fabs(y + anchor[1] - p[1]) <= 1e-4;
    }
    CHECK(placed, "where the host put them");
    CHECK(Array(&scene, mvfx_attributeVelocityZ)[0] == 9.0f &&
              Array(&scene, mvfx_attributeColorR)[0] == 1.0f &&
              Array(&scene, mvfx_attributeColorG)[1] == 1.0f,
          "given values kept");
    CHECK(fabsf(Array(&scene, mvfx_attributeVelocityX)[2]) <= 1e-5f &&
              fabsf(Array(&scene, mvfx_attributeVelocityY)[2] - 2.0f) <= 1e-5f &&
              Array(&scene, mvfx_attributeColorR)[2] == 0.0f,
          "drawn values turned with the instance");
    CHECK(scene.live->emitters[0].hostCount == 0, "the queue emptied");
    mvfxDestroyContext(scene.context);
}

static void TestDrawnFromCenterAndLocal(void)
{
    // From the center: a host particle sets off away from the instance's
    // origin. In local space, places are the instance's.
    for (int32_t space = 0; space < 2; ++space)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.rate = 0.0f;
        emitter.space = (mvfxSimulationSpace)space;
        emitter.hostSpawnCapacity = 4;
        emitter.velocity = (mvfxVelocity){.mode = mvfx_directionFromCenter, .speed = {1.0f, 1.0f}};
        Scene scene = NewScene(&emitter);
        const double origin[3] = {space == 0 ? 50.0 : 0.0, 0.0, 0.0};
        CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin,
                                        (const float[4]){0, 0, 0, 1}, 1.0f, true) == mvfx_success,
              "placed");
        const double place[3] = {origin[0], 3.0, 0.0};
        mvfxSpawnRequest request = {.count = 1, .positions = place};
        CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
              "queued");
        CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
        CHECK(scene.live->emitters[0].count == 1 &&
                  fabsf(Array(&scene, mvfx_attributeVelocityY)[0] - 1.0f) <= 1e-6f &&
                  fabsf(Array(&scene, mvfx_attributePositionY)[0] - 3.0f) <= 1e-6f,
              "away from the instance, where it was put");
        mvfxDestroyContext(scene.context);
    }
}

static void TestEdges(void)
{
    // A rebased anchor, a moving instance's inherited velocity, a drawn
    // velocity at the instance's very center, given sizes.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.inheritVelocity = 1.0f;
    emitter.lifetime = mvfxRangeSource(5.0f, 5.0f);
    emitter.size =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1, 0}, .max = {1, 1, 1, 0}};
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionFromCenter, .speed = {1.0f, 1.0f}};
    Scene scene = NewScene(&emitter);
    const float unturned[4] = {0, 0, 0, 1};
    const double far[3] = {2000.0, 0.0, 0.0};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, far, unturned, 1.0f, true) ==
              mvfx_success,
          "teleported");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(scene.live->emitters[0].anchor[0] == 2000.0, "rebased");
    const double next[3] = {2004.0, 0.0, 0.0};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, next, unturned, 1.0f, false) ==
              mvfx_success,
          "moving");
    // At the instance's place at the step's start, its center.
    static const double places[3] = {2000.0, 0.0, 0.0};
    static const float sizes[3] = {4.0f, 5.0f, 6.0f};
    mvfxSpawnRequest request = {.count = 1, .positions = places, .sizes = sizes};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success, "queued");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    CHECK(scene.live->emitters[0].count == 1, "born");
    CHECK(fabsf(Array(&scene, mvfx_attributePositionX)[0] -
                0.5f * Array(&scene, mvfx_attributeVelocityX)[0]) <= 1e-5f,
          "an offset from the moved anchor, then half a second's flight");
    float vx = Array(&scene, mvfx_attributeVelocityX)[0] - 8.0f;
    float vy = Array(&scene, mvfx_attributeVelocityY)[0];
    float vz = Array(&scene, mvfx_attributeVelocityZ)[0];
    CHECK(fabsf(sqrtf(vx * vx + vy * vy + vz * vz) - 1.0f) <= 1e-5f,
          "any way at speed 1 from the center, plus the instance's 8 along X");
    CHECK(Array(&scene, mvfx_attributeSizeX)[0] == 4.0f &&
              Array(&scene, mvfx_attributeSizeZ)[0] == 6.0f,
          "given sizes kept");
    mvfxDestroyContext(scene.context);

    // Along the shape's normal at the center: any way, at speed 1.
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionShapeNormal, .speed = {1.0f, 1.0f}};
    emitter.inheritVelocity = 0.0f;
    scene = NewScene(&emitter);
    static const double center[3] = {0.0, 0.0, 0.0};
    mvfxSpawnRequest atCenter = {.count = 1, .positions = center};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &atCenter) == mvfx_success,
          "queued");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    vx = Array(&scene, mvfx_attributeVelocityX)[0];
    vy = Array(&scene, mvfx_attributeVelocityY)[0];
    vz = Array(&scene, mvfx_attributeVelocityZ)[0];
    CHECK(fabsf(sqrtf(vx * vx + vy * vy + vz * vz) - 1.0f) <= 1e-5f,
          "a normal any way at the center");
    mvfxDestroyContext(scene.context);

    // A host particle whose life ends within the step is owed, not kept.
    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 1;
    emitter.lifetime = mvfxRangeSource(0.1f, 0.1f);
    scene = NewScene(&emitter);
    mvfxSpawnRequest one = {.count = 1, .positions = places};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &one) == mvfx_success, "queued");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.live == 0 && stats.owed == 1 && stats.refused == 0, "lived and ended in the step");
    mvfxDestroyContext(scene.context);
}

static void TestQueueAndRoom(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 2;
    emitter.hostSpawnCapacity = 2;
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceRange};
    Scene scene = NewScene(&emitter);
    static const double places[9] = {0};
    mvfxSpawnRequest one = {.count = 1, .positions = places};
    mvfxSpawnRequest two = {.count = 2, .positions = places};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &one) == mvfx_success, "one");
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &two) == mvfx_errorCapacity,
          "past the queue's room");
    CHECK(scene.live->emitters[0].hostCount == 1, "none of a refused request queued");
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &one) == mvfx_success, "one more");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &two) == mvfx_success, "two");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.live == 2 && stats.owed == 4 && stats.refused == 2, "a full emitter refuses");

    // Stopped, the host's still come; a restart empties the queue.
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopAndClear) == mvfx_success,
          "stopped");
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance), "finished");
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &one) == mvfx_success, "queued");
    CHECK(!mvfxInstance_IsFinished(scene.context, scene.instance), "not with a queue");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(Stats(&scene).live == 1, "born while stopped");
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &one) == mvfx_success, "queued");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(scene.live->emitters[0].hostCount == 0, "a restart empties the queue");

    // Refusals.
    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    static const double far[3] = {0x1p53, 0, 0};
    static const float nan[3] = {0, (float)NAN, 0};
    static const float sizes[3] = {1, 1, 1};
    mvfxSpawnRequest bad[6] = {
        {.count = 0, .positions = places},
        {.count = 1},
        {.count = 1, .positions = far},
        {.count = 1, .positions = places, .velocities = nan},
        {.count = 1, .positions = places, .sizes = sizes},
        {.count = 1, .positions = places, .colors = nan},
    };
    for (int32_t i = 0; i < 6; ++i)
    {
        CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &bad[i]) == mvfx_errorInvalid,
              "refused");
    }
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 1, &one) == mvfx_errorInvalid,
          "no emitter 1");
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, NULL) == mvfx_errorInvalid,
          "no request");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 8, "each counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_Spawn(scene.context, stale, 0, &one) == mvfx_errorStale, "stale");
    CHECK(mvfxInstance_Spawn(NULL, scene.instance, 0, &one) == mvfx_errorInvalid, "no context");
    mvfxDestroyContext(scene.context);

    // A queue past the capacity is refused when the effect is made.
    emitter.hostSpawnCapacity = 3;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid,
          "a queue past the capacity");
    mvfxDestroyContext(context);
}

static void TestPrewarm(void)
{
    // Three seconds in quarter-second steps, five a call, against the same
    // twelve steps taken at once.
    mvfxBurst burst = {.time = 0.1f,
                       .minCount = 3,
                       .maxCount = 9,
                       .cycles = 0,
                       .interval = 0.3f,
                       .probability = 0.8f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 60;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.3f, 1.4f);
    Scene warmed = NewScene(&emitter);
    Scene stepped = NewScene(&emitter);
    double left = 3.0;
    int32_t calls = 0;
    while (left > 0.0 && calls < 10)
    {
        CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, left, 0.25, 5, &left) ==
                  mvfx_success,
              "prewarm");
        ++calls;
    }
    CHECK(calls == 3 && left == 0.0, "three calls of five, five and two steps");
    for (int32_t i = 0; i < 12; ++i)
    {
        CHECK(mvfxContext_StepTicks(stepped.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success,
              "step");
    }
    CHECK(mvfxHashInstance(warmed.live, 1) == mvfxHashInstance(stepped.live, 1),
          "the same state as the steps taken at once");
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, 0.1, 0.25, 1, NULL) == mvfx_success,
          "a short span, no out-parameter");
    CHECK(warmed.live->clock == 3 * MVFX_TICKS_PER_SECOND + 104858, "the last step shorter");

    uint64_t misuse = mvfxContext_GetMisuse(warmed.context);
    double out = 7.0;
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, -1.0, 0.25, 1, &out) ==
                  mvfx_errorInvalid &&
              out == 0.0,
          "a negative span, zeroed");
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, 1.0, 0.0, 1, NULL) ==
              mvfx_errorInvalid,
          "a step of 0");
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, 1.0, 2.0, 1, NULL) ==
              mvfx_errorInvalid,
          "a step past a second");
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, 1.0, 1e-9, 1, NULL) ==
              mvfx_errorInvalid,
          "a step under a tick");
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, 1.0, 0.25, 0, NULL) ==
              mvfx_errorInvalid,
          "no steps");
    CHECK(mvfxInstance_Prewarm(warmed.context, warmed.instance, (double)NAN, 0.25, 1, NULL) ==
              mvfx_errorInvalid,
          "a NaN span");
    CHECK(mvfxContext_GetMisuse(warmed.context) == misuse + 6, "each counted");
    mvfxInstanceId stale = warmed.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_Prewarm(warmed.context, stale, 1.0, 0.25, 1, NULL) == mvfx_errorStale,
          "stale");
    CHECK(mvfxInstance_Prewarm(NULL, warmed.instance, 1.0, 0.25, 1, NULL) == mvfx_errorInvalid,
          "no context");
    mvfxDestroyContext(warmed.context);
    mvfxDestroyContext(stepped.context);
}

int main(void)
{
    TestWorldSpawns();
    TestDrawnFromCenterAndLocal();
    TestEdges();
    TestQueueAndRoom();
    TestPrewarm();
    return s_failures == 0 ? 0 : 1;
}
