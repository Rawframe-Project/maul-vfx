// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawning from events: a death's particles born at its tick and place
// with a share of its velocity; a source after its consumer in the def
// still stepping first; counts drawn in their range and fixed by their
// inputs; the colour and size inherited (dropped by a consumer storing
// neither); a local emitter's births mapped
// through the instance; a full consumer taking the earliest events'
// births and refusing the rest; a stop ending the spawns; a consumer
// keeping its instance going while its source lives; and the refusals,
// a cycle among them.

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

static Scene NewScene(const mvfxEmitterDef* emitters, int32_t count)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

// A source of what the host spawns, telling of its deaths.
static mvfxEmitterDef Source(float minLife, float maxLife)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 64;
    emitter.hostSpawnCapacity = 64;
    emitter.lifetime = mvfxRangeSource(minLife, maxLife);
    emitter.events = mvfx_eventMaskDeath;
    emitter.eventCapacity = 64;
    return emitter;
}

// An emitter spawning count particles a death of source, living ten
// seconds.
static mvfxEmitterDef Consumer(int32_t source, int32_t minCount, int32_t maxCount)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 256;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.eventSpawn = (mvfxEventSpawn){.enabled = true,
                                          .source = source,
                                          .kind = mvfx_eventDeath,
                                          .minCount = minCount,
                                          .maxCount = maxCount,
                                          .inheritVelocity = 0.5f,
                                          .capacity = 64};
    return emitter;
}

static void Place(Scene* scene, int32_t emitter, double x, double y, double z, float vx)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, emitter, &request) == mvfx_success,
          "placed");
}

static void Step(Scene* scene, int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(scene->context, ticks) == mvfx_success, "step");
}

static float At(const Scene* scene, int32_t emitter, mvfxAttribute attribute, int32_t i)
{
    return ((const float*)mvfxAttributeArray(scene->live, emitter, attribute))[i];
}

static int64_t BirthOf(const Scene* scene, int32_t emitter, int32_t i)
{
    const uint32_t* low = mvfxAttributeArray(scene->live, emitter, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(scene->live, emitter, mvfx_attributeBirthHigh);
    return (int64_t)(((uint64_t)high[i] << 32u) | low[i]);
}

static void TestDeathSpawns(void)
{
    // A source particle at (1, 2, 3) moving 2 along X lives half a second:
    // three particles born at its death tick where it last was, moving 1
    // (half its 2), so at x = 1.5 by the step's end. The consumer comes
    // first in the def; its source steps first all the same.
    mvfxEmitterDef emitters[2] = {Consumer(1, 3, 3), Source(0.5f, 0.5f)};
    Scene scene = NewScene(emitters, 2);
    Place(&scene, 1, 1, 2, 3, 2.0f);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    const mvfxEmitterState* consumer = &scene.live->emitters[0];
    CHECK(consumer->count == 3, "three a death");
    bool all = true;
    for (int32_t i = 0; i < 3; ++i)
    {
        double lastX = 1.0 + 2.0 / MVFX_TICKS_PER_SECOND;
        all = all && BirthOf(&scene, 0, i) == MVFX_TICKS_PER_SECOND / 2 &&
              fabs((double)At(&scene, 0, mvfx_attributePositionX, i) - (lastX + 0.5)) <= 1e-5 &&
              At(&scene, 0, mvfx_attributePositionY, i) == 2.0f &&
              At(&scene, 0, mvfx_attributeVelocityX, i) == 1.0f;
    }
    CHECK(all, "at its tick and place with half its velocity");
    mvfxDestroyContext(scene.context);
}

static void TestCounts(void)
{
    // From 0 to 4 a death over 40 deaths: within the range, more than one
    // value, and the same twice over.
    int32_t totals[2] = {0, 0};
    for (int32_t run = 0; run < 2; ++run)
    {
        mvfxEmitterDef emitters[2] = {Source(0.5f, 0.5f), Consumer(0, 0, 4)};
        Scene scene = NewScene(emitters, 2);
        for (int32_t i = 0; i < 40; ++i)
        {
            Place(&scene, 0, i, 0, 0, 0.0f);
        }
        Step(&scene, 1);
        Step(&scene, MVFX_TICKS_PER_SECOND - 1);
        totals[run] = scene.live->emitters[1].count;
        mvfxDestroyContext(scene.context);
    }
    CHECK(totals[0] == totals[1] && totals[0] > 40 && totals[0] < 120, "drawn in range, fixed");
}

static void TestInherit(void)
{
    // The colour and size of the source's particle at its death.
    mvfxEmitterDef emitters[2] = {Source(0.5f, 0.5f), Consumer(0, 1, 1)};
    emitters[0].color = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                           .min = {0.25f, 0.5f, 0.75f, 1.0f},
                                           .max = {0.25f, 0.5f, 0.75f, 1.0f}};
    emitters[0].size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {3, 3, 3, 0}, .max = {3, 3, 3, 0}, .uniform = true};
    emitters[1].color =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1, 1}, .max = {1, 1, 1, 1}};
    emitters[1].size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {1, 1, 1, 0}, .max = {1, 1, 1, 0}, .uniform = true};
    emitters[1].eventSpawn.inheritColor = true;
    emitters[1].eventSpawn.inheritSize = true;
    Scene scene = NewScene(emitters, 2);
    Place(&scene, 0, 0, 0, 0, 0.0f);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    CHECK(scene.live->emitters[1].count == 1 && At(&scene, 1, mvfx_attributeColorG, 0) == 0.5f &&
              At(&scene, 1, mvfx_attributeSizeX, 0) == 3.0f,
          "its colour and size");
    mvfxDestroyContext(scene.context);
    // A consumer storing neither keeps its places: what it inherits goes.
    emitters[1].color = (mvfxVectorSource){0};
    emitters[1].size = (mvfxVectorSource){0};
    scene = NewScene(emitters, 2);
    Place(&scene, 0, 4, 5, 6, 0.0f);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    CHECK(scene.live->emitters[1].count == 1 && At(&scene, 1, mvfx_attributePositionX, 0) == 4.0f &&
              At(&scene, 1, mvfx_attributePositionY, 0) == 5.0f &&
              At(&scene, 1, mvfx_attributePositionZ, 0) == 6.0f,
          "a consumer storing no colour or size keeps its places");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // A local source and consumer under an instance at (100, 0, 0), turned
    // a quarter about Z, at scale 2: the death at the source's (1, 0, 0)
    // moving (1, 0, 0) is born at the consumer's (1, 0, 0) moving (0.5, 0,
    // 0), half the inherited 1 the world's 2 makes back in its space, so at
    // x = 1.25 by the step's end.
    mvfxEmitterDef emitters[2] = {Source(0.5f, 0.5f), Consumer(0, 1, 1)};
    emitters[0].space = mvfx_spaceLocal;
    emitters[1].space = mvfx_spaceLocal;
    Scene scene = NewScene(emitters, 2);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 0, 1, 0, 0, 1.0f);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    CHECK(scene.live->emitters[1].count == 1 &&
              fabsf(At(&scene, 1, mvfx_attributePositionX, 0) - 1.25f) <= 1e-4f &&
              fabsf(At(&scene, 1, mvfx_attributePositionY, 0)) <= 1e-5f &&
              fabsf(At(&scene, 1, mvfx_attributeVelocityX, 0) - 0.5f) <= 1e-6f,
          "mapped through the instance");
    mvfxDestroyContext(scene.context);
}

static void TestFullAndStopped(void)
{
    // Two source particles with lives from 0.2 to 0.8 seconds; a consumer
    // of room for one takes the earlier death's birth, the later refused,
    // whichever is first in the source.
    mvfxEmitterDef emitters[2] = {Source(0.2f, 0.8f), Consumer(0, 1, 1)};
    emitters[1].capacity = 1;
    Scene scene = NewScene(emitters, 2);
    Place(&scene, 0, 0, 0, 0, 0.0f);
    Place(&scene, 0, 1, 0, 0, 0.0f);
    Step(&scene, 1);
    const float* inverse = mvfxAttributeArray(scene.live, 0, mvfx_attributeInverseLife);
    double first = fmin(1.0 / (double)inverse[0], 1.0 / (double)inverse[1]);
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) == mvfx_success &&
              scene.live->emitters[1].count == 1 && stats.refused == 1 &&
              fabs((double)BirthOf(&scene, 1, 0) / MVFX_TICKS_PER_SECOND - first) <= 2e-6,
          "the earlier death's, the later refused");
    mvfxDestroyContext(scene.context);

    // Stopped: the source lives on, the consumer keeps the instance going,
    // its death spawns nothing past the stop, and then the instance
    // finishes.
    mvfxEmitterDef pair[2] = {Source(0.5f, 0.5f), Consumer(0, 1, 1)};
    scene = NewScene(pair, 2);
    Place(&scene, 0, 0, 0, 0, 0.0f);
    Step(&scene, 1);
    CHECK(mvfxInstance_Stop(scene.context, scene.instance, mvfx_stopEmitting) == mvfx_success,
          "stopped");
    Step(&scene, 1);
    CHECK(!mvfxInstance_IsFinished(scene.context, scene.instance), "its source lives");
    Step(&scene, MVFX_TICKS_PER_SECOND - 2);
    CHECK(scene.live->emitters[1].count == 0, "nothing past the stop");
    Step(&scene, 1);
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance), "then finished");
    mvfxDestroyContext(scene.context);

    // A consumer of one loop of 0.3 seconds: a death at 0.5 spawns nothing;
    // of two loops, it does.
    for (int32_t loops = 1; loops <= 2; ++loops)
    {
        mvfxEmitterDef looped[2] = {Source(0.5f, 0.5f), Consumer(0, 1, 1)};
        looped[1].duration = 0.3f;
        looped[1].loops = loops;
        scene = NewScene(looped, 2);
        Place(&scene, 0, 0, 0, 0, 0.0f);
        Step(&scene, 1);
        Step(&scene, MVFX_TICKS_PER_SECOND - 1);
        CHECK(scene.live->emitters[1].count == loops - 1, "spawns within its loops only");
        mvfxDestroyContext(scene.context);
    }

    // Not stopped, a consumer whose source is empty and spawns nothing more
    // lets the instance finish.
    mvfxEmitterDef idle[2] = {Source(0.5f, 0.5f), Consumer(0, 1, 1)};
    scene = NewScene(idle, 2);
    Step(&scene, 1);
    CHECK(mvfxInstance_IsFinished(scene.context, scene.instance), "nothing ahead");
    mvfxDestroyContext(scene.context);
}

static void TestRoomAndStatus(void)
{
    // An event spawning 3 with a step's capacity of 1 from events: one
    // born, two refused, three serials spent.
    mvfxEmitterDef emitters[2] = {Source(0.5f, 0.5f), Consumer(0, 3, 3)};
    emitters[1].eventSpawn.capacity = 1;
    Scene scene = NewScene(emitters, 2);
    Place(&scene, 0, 0, 0, 0, 0.0f);
    Step(&scene, 1);
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) == mvfx_success &&
              stats.live == 1 && stats.refused == 2 && stats.owed == 3,
          "three serials, two refused");
    mvfxDestroyContext(scene.context);

    // Room for one, lives of a quarter second: the earlier death's birth
    // dies before the later death, which then finds room.
    mvfxEmitterDef full[2] = {Source(0.2f, 0.8f), Consumer(0, 1, 1)};
    full[1].capacity = 1;
    full[1].lifetime = mvfxRangeSource(0.25f, 0.25f);
    scene = NewScene(full, 2);
    Place(&scene, 0, 0, 0, 0, 0.0f);
    Place(&scene, 0, 1, 0, 0, 0.0f);
    Step(&scene, 1);
    const float* inverse = mvfxAttributeArray(scene.live, 0, mvfx_attributeInverseLife);
    double early = fmin(1.0 / (double)inverse[0], 1.0 / (double)inverse[1]);
    double late = fmax(1.0 / (double)inverse[0], 1.0 / (double)inverse[1]);
    CHECK(late - early > 0.26 && late + 0.25 > 1.0, "the lives this needs");
    Step(&scene, MVFX_TICKS_PER_SECOND - 1);
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) == mvfx_success &&
              stats.live == 1 && stats.refused == 0 &&
              fabs((double)BirthOf(&scene, 1, 0) / MVFX_TICKS_PER_SECOND - late) <= 2e-6,
          "room again after a death within the step");
    mvfxDestroyContext(scene.context);

    // A consumer is active while its source has particles, or births
    // ahead (a rate after a delay), though it has none itself.
    mvfxEmitterDef alive[2] = {Source(5.0f, 5.0f), Consumer(0, 1, 1)};
    scene = NewScene(alive, 2);
    Place(&scene, 0, 0, 0, 0, 0.0f);
    Step(&scene, 1);
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) == mvfx_success &&
              stats.status == mvfx_emitterActive,
          "active while its source has particles");
    mvfxDestroyContext(scene.context);
    mvfxEmitterDef ahead[2] = {Source(5.0f, 5.0f), Consumer(0, 1, 1)};
    ahead[0].rate = 10.0f;
    ahead[0].startDelay = 1.0f;
    scene = NewScene(ahead, 2);
    Step(&scene, 1);
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) == mvfx_success &&
              stats.status == mvfx_emitterActive,
          "active while its source has births ahead");
    mvfxDestroyContext(scene.context);

    // One particle bouncing between walls collides every step: the counts
    // it draws (0 to 4) differ from step to step, the tick in the draw.
    static const mvfxCollider walls[2] = {
        {.kind = mvfx_colliderPlane, .center = {-1, 0, 0}, .normal = {1, 0, 0}},
        {.kind = mvfx_colliderPlane, .center = {1, 0, 0}, .normal = {-1, 0, 0}},
    };
    mvfxEmitterDef bouncing[2] = {Source(100.0f, 100.0f), Consumer(0, 0, 4)};
    bouncing[0].events = mvfx_eventMaskCollision;
    bouncing[0].collision = (mvfxCollision){.enabled = true, .restitution = 1.0f};
    bouncing[0].colliders = walls;
    bouncing[0].colliderCount = 2;
    bouncing[1].eventSpawn.kind = mvfx_eventCollision;
    scene = NewScene(bouncing, 2);
    Place(&scene, 0, 0, 0, 0, 3.0f);
    uint64_t last = 0;
    int32_t seen[5] = {0};
    for (int32_t step = 0; step < 12; ++step)
    {
        Step(&scene, MVFX_TICKS_PER_SECOND);
        CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) ==
                  mvfx_success,
              "stats");
        // One collision a step (its first contact) spawns.
        uint64_t drawn = stats.owed - last;
        last = stats.owed;
        seen[drawn <= 4 ? drawn : 4] += 1;
    }
    int32_t kinds = 0;
    for (int32_t c = 0; c < 5; ++c)
    {
        kinds += seen[c] > 0 ? 1 : 0;
    }
    CHECK(kinds >= 2, "counts differ by the tick");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 13; ++field)
    {
        mvfxEmitterDef emitters[3] = {Source(0.5f, 0.5f), Consumer(0, 1, 2), Source(0.5f, 0.5f)};
        mvfxEventSpawn* spawn = &emitters[1].eventSpawn;
        switch (field)
        {
        case 0:
            spawn->source = 1;
            break;
        case 1:
            spawn->source = -1;
            break;
        case 2:
            spawn->source = 3;
            break;
        case 3:
            spawn->kind = mvfx_eventBirth;
            break;
        case 4:
            spawn->kind = 3;
            break;
        case 5:
            spawn->minCount = 3;
            break;
        case 6:
            spawn->minCount = -1;
            break;
        case 7:
            spawn->maxCount = MVFX_MAX_EVENT_SPAWN + 1;
            break;
        case 8:
            spawn->inheritVelocity = 1.5f;
            break;
        case 9:
            spawn->inheritVelocity = (float)NAN;
            break;
        case 10:
            spawn->capacity = 0;
            break;
        case 11:
            spawn->capacity = MVFX_MAX_EMITTER_CAPACITY + 1;
            break;
        default:
            // A cycle: the source spawns from its consumer's deaths.
            emitters[1].events = mvfx_eventMaskDeath;
            emitters[1].eventCapacity = 8;
            emitters[0].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                                      .source = 1,
                                                      .kind = mvfx_eventDeath,
                                                      .maxCount = 1,
                                                      .capacity = 8};
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = emitters;
        effectDef.emitterCount = 3;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "refused");
    }
    // A chain is no cycle: 2 from 1's deaths, 1 from 0's.
    mvfxEmitterDef chain[3] = {Source(0.5f, 0.5f), Consumer(0, 1, 1), Consumer(1, 1, 1)};
    chain[1].events = mvfx_eventMaskDeath;
    chain[1].eventCapacity = 8;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = chain;
    effectDef.emitterCount = 3;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "a chain");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestDeathSpawns();
    TestCounts();
    TestInherit();
    TestLocal();
    TestFullAndStopped();
    TestRoomAndStatus();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
