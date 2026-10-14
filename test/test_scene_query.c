// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scene queries: a moving particle's query, its place and segment in the
// world; an answered plane bounced off in the step after, none answered
// let through; a particle at rest asking nothing and keeping its plane;
// the capacity and the dropped; a local emitter's queries and answers
// mapped through a moved, turned, scaled instance; planes moving with a
// world emitter's rebase; births without a plane; answers consumed and
// a restart clearing them; and the refusals.

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
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

// An emitter of what the host spawns, asking the scene for up to capacity
// planes a step half a second ahead, bouncing fully.
static mvfxEmitterDef Asking(int32_t capacity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.collision = (mvfxCollision){.enabled = true,
                                        .restitution = 1.0f,
                                        .sceneQueries = true,
                                        .queryCapacity = capacity,
                                        .lookahead = 0.5f};
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

static double At(const Scene* scene, mvfxAttribute attribute, int32_t i)
{
    return (double)((const float*)mvfxAttributeArray(scene->live, 0, attribute))[i];
}

static bool Near(double a, double b, double within)
{
    return fabs(a - b) <= within;
}

static void Step(Scene* scene, int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(scene->context, ticks) == mvfx_success, "step");
}

static int32_t Queries(Scene* scene, const mvfxSceneQuery** queries)
{
    int32_t count = -1;
    CHECK(mvfxInstance_GetSceneQueries(scene->context, scene->instance, 0, queries, &count) ==
              mvfx_success,
          "queries");
    return count;
}

// Answers every query with the plane y = height, up, or with none.
static void AnswerFloor(Scene* scene, bool hit, double height)
{
    const mvfxSceneQuery* queries = NULL;
    int32_t count = Queries(scene, &queries);
    mvfxSceneAnswer answers[8] = {0};
    for (int32_t a = 0; a < count; ++a)
    {
        answers[a] = (mvfxSceneAnswer){.hit = hit, .point = {0, height, 0}, .normal = {0, 1, 0}};
    }
    CHECK(mvfxInstance_AnswerSceneQueries(scene->context, scene->instance, 0, answers, count) ==
              mvfx_success,
          "answered");
}

static void TestQueryAndAnswer(void)
{
    mvfxEmitterDef emitter = Asking(4);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 3, 0, 0, -4, 0);
    Step(&scene, 1);
    const mvfxSceneQuery* queries = NULL;
    CHECK(Queries(&scene, &queries) == 1, "one query");
    const uint32_t* serial = mvfxAttributeArray(scene.live, 0, mvfx_attributeSerial);
    CHECK(queries[0].particle == 0 && queries[0].serial == serial[0] &&
              Near(queries[0].origin[1], 3.0, 1e-4) && queries[0].origin[0] == 0.0 &&
              Near((double)queries[0].segment[1], -2.0, 1e-6),
          "its place and segment");
    AnswerFloor(&scene, true, 0.0);
    CHECK(Queries(&scene, &queries) == 0 && queries == NULL, "consumed");
    // Down 4 for a second from y = 3: off the plane, back up to 1.
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Near(At(&scene, mvfx_attributePositionY, 0), 1.0, 1e-3) &&
              Near(At(&scene, mvfx_attributeVelocityY, 0), 4.0, 1e-5),
          "bounced off the answered plane");
    const uint32_t* collisions = mvfxAttributeArray(scene.live, 0, mvfx_attributeCollisions);
    CHECK(collisions[0] == 1, "a collision counted");
    // Answered none: through where the plane was.
    AnswerFloor(&scene, false, 0.0);
    Place(&scene, 0, 1, 0, 0, -4, 0);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Near(At(&scene, mvfx_attributePositionY, 0), 5.0, 1e-3), "the first rises on");
    CHECK(Near(At(&scene, mvfx_attributePositionY, 1), -3.0, 1e-3), "a birth has no plane");
    AnswerFloor(&scene, false, 0.0);
    Place(&scene, 0, 1, 0, 0, -4, 0);
    Step(&scene, 1);
    AnswerFloor(&scene, false, 0.0);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Near(At(&scene, mvfx_attributePositionY, 2), -3.0, 1e-3), "answered none, through");
    mvfxDestroyContext(scene.context);
}

static void TestRestAndCapacity(void)
{
    // Falling under gravity onto an answered plane, stopping below the rest
    // speed: at rest it asks nothing and keeps its plane, steps on.
    mvfxEmitterDef emitter = Asking(4);
    emitter.motion.gravity[1] = -10.0f;
    emitter.collision.restSpeed = 100.0f;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0.5, 0, 0, -1, 0);
    Step(&scene, 1);
    AnswerFloor(&scene, true, 0.0);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    const mvfxSceneQuery* queries = NULL;
    CHECK(At(&scene, mvfx_attributeVelocityY, 0) == 0.0 && Queries(&scene, &queries) == 0,
          "at rest, no query");
    for (int32_t s = 0; s < 5; ++s)
    {
        Step(&scene, MVFX_TICKS_PER_SECOND / 4);
    }
    CHECK(Near(At(&scene, mvfx_attributePositionY, 0), 0.0, 1e-4), "resting on its plane");
    mvfxDestroyContext(scene.context);

    // Two queries a step: three of five moving particles dropped, and one
    // at rest not asking.
    emitter = Asking(2);
    scene = NewScene(&emitter);
    for (int32_t i = 0; i < 5; ++i)
    {
        Place(&scene, i, 0, 0, 1, 0, 0);
    }
    Place(&scene, 9, 0, 0, 0, 0, 0);
    Step(&scene, 1);
    CHECK(Queries(&scene, &queries) == 2 && queries[0].particle == 0 && queries[1].particle == 1,
          "the first two");
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) == mvfx_success &&
              stats.queriesDropped == 3,
          "three dropped");
    // Unanswered, the next step writes them again; a restart clears them
    // and the count.
    Step(&scene, 1);
    CHECK(Queries(&scene, &queries) == 2, "written again");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(Queries(&scene, &queries) == 0, "cleared");
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) == mvfx_success &&
              stats.queriesDropped == 0,
          "count cleared");
    mvfxDestroyContext(scene.context);
}

static void TestBirthInPlace(void)
{
    // A particle with a plane ends; one born in its slot has none.
    mvfxEmitterDef emitter = Asking(4);
    emitter.lifetime = mvfxRangeSource(1.5f, 1.5f);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0.5, 0, 0, -1, 0);
    Step(&scene, 1);
    AnswerFloor(&scene, true, 0.0);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    Place(&scene, 0, 1, 0, 0, -4, 0);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(scene.live->emitters[0].count == 1 &&
              Near(At(&scene, mvfx_attributePositionY, 0), -3.0, 1e-3),
          "born in its slot without its plane");
    mvfxDestroyContext(scene.context);
}

static void TestSpaces(void)
{
    // A local emitter under an instance at (1000, 0, 0), turned a quarter
    // about Z, at scale 2: the particle at its (0, 1, 0) moving (0, -1, 0)
    // is at the world's (998, 0, 0), its half second's segment (1, 0, 0).
    mvfxEmitterDef emitter = Asking(4);
    emitter.space = mvfx_spaceLocal;
    emitter.collision.radius = 0.25f;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {1000.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 0, 1, 0, 0, -1, 0);
    Step(&scene, 1);
    const mvfxSceneQuery* queries = NULL;
    CHECK(Queries(&scene, &queries) == 1 && Near(queries[0].origin[0], 998.0, 1e-3) &&
              Near(queries[0].origin[1], 0.0, 1e-4) &&
              Near((double)queries[0].segment[0], 1.0, 1e-5) &&
              Near((double)queries[0].segment[1], 0.0, 1e-5),
          "mapped into the world");
    // The world's plane x = 999.5 facing -X is the emitter's y = 0.25 up;
    // the particle's radius of 0.25 in its own space keeps it to y = 0.5,
    // met halfway down from 1 and back up to 1.
    mvfxSceneAnswer answer = {.hit = true, .point = {999.5, 0, 0}, .normal = {-1, 0, 0}};
    CHECK(mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &answer, 1) ==
              mvfx_success,
          "answered");
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Near(At(&scene, mvfx_attributePositionY, 0), 1.0, 1e-3) &&
              Near(At(&scene, mvfx_attributeVelocityY, 0), 1.0, 1e-5),
          "the answer mapped back");
    mvfxDestroyContext(scene.context);

    // A world emitter's planes move with its rebase: its instance jumps
    // 5000 up, its anchor follows, and the particle still meets the
    // world's y = 0.
    emitter = Asking(4);
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -1, 0);
    Step(&scene, 1);
    AnswerFloor(&scene, true, 0.0);
    static const double far[3] = {0.0, 5000.0, 0.0};
    static const float still[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, far, still, 1.0f, true) ==
              mvfx_success,
          "jumped");
    Step(&scene, MVFX_TICKS_PER_SECOND);
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(scene.live->emitters[0].anchor[1] == 5000.0, "rebased");
    // The hair it is left off the plane by grows with its place's size,
    // 5000 here.
    CHECK(Near(At(&scene, mvfx_attributePositionY, 0), 1.0 - 5000.0, 1e-2), "still met the plane");
    mvfxDestroyContext(scene.context);
}

static void TestAxesAndMotion(void)
{
    // Moving only along Z, a plane facing only along Z: from z = -3 at 4,
    // off z = 0 and back to -1.
    mvfxEmitterDef emitter = Asking(4);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, -3, 0, 0, 4);
    Step(&scene, 1);
    const mvfxSceneQuery* queries = NULL;
    CHECK(Queries(&scene, &queries) == 1, "a query along Z");
    mvfxSceneAnswer answer = {.hit = true, .normal = {0, 0, -1}};
    CHECK(mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &answer, 1) ==
              mvfx_success,
          "answered");
    Step(&scene, MVFX_TICKS_PER_SECOND);
    CHECK(Near(At(&scene, mvfx_attributePositionZ, 0), -1.0, 1e-3), "a plane facing along Z");
    // Unanswered, the next step writes its one query again, not two.
    Step(&scene, 1);
    CHECK(Queries(&scene, &queries) == 1, "written anew");
    mvfxDestroyContext(scene.context);

    // A local emitter's instance moving to (10, 0, 0) over a step: the
    // query is written where it ends.
    emitter = Asking(4);
    emitter.space = mvfx_spaceLocal;
    scene = NewScene(&emitter);
    static const double to[3] = {10.0, 0.0, 0.0};
    static const float still[4] = {0, 0, 0, 1};
    Place(&scene, 0, 0, 0, 0, 1, 0);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, to, still, 1.0f, false) ==
              mvfx_success,
          "moving");
    Step(&scene, 1);
    CHECK(Queries(&scene, &queries) == 1 && Near(queries[0].origin[0], 10.0, 1e-4),
          "at the step's end");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Asking(4);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 5, 0, 0, -4, 0);
    Step(&scene, 1);
    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    const mvfxSceneQuery* queries = NULL;
    int32_t count = 0;
    mvfxSceneAnswer good = {.hit = true, .normal = {0, 1, 0}};
    mvfxSceneAnswer bad[5] = {good, good, good, good, {.hit = false, .normal = {(float)NAN}}};
    bad[0].normal[1] = 0.0f;
    bad[1].normal[2] = (float)INFINITY;
    bad[2].point[0] = 0x1p53;
    bad[3].point[1] = (double)NAN;
    CHECK(mvfxInstance_GetSceneQueries(scene.context, scene.instance, 0, NULL, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_GetSceneQueries(scene.context, scene.instance, 0, &queries, NULL) ==
                  mvfx_errorInvalid &&
              mvfxInstance_GetSceneQueries(scene.context, scene.instance, 1, &queries, &count) ==
                  mvfx_errorInvalid,
          "reads refused");
    for (int32_t b = 0; b < 4; ++b)
    {
        CHECK(mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &bad[b], 1) ==
                  mvfx_errorInvalid,
              "an answer out of range");
    }
    CHECK(mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &good, 2) ==
                  mvfx_errorInvalid &&
              mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &good, 0) ==
                  mvfx_errorInvalid &&
              mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, NULL, 1) ==
                  mvfx_errorInvalid &&
              mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, -1, &good, 1) ==
                  mvfx_errorInvalid,
          "counts and emitters refused");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 11, "each counted");
    CHECK(Queries(&scene, &queries) == 1, "the queries kept");
    // A miss's fields are not read.
    CHECK(mvfxInstance_AnswerSceneQueries(scene.context, scene.instance, 0, &bad[4], 1) ==
              mvfx_success,
          "a miss");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(
        mvfxInstance_GetSceneQueries(scene.context, stale, 0, &queries, &count) ==
                mvfx_errorStale &&
            mvfxInstance_AnswerSceneQueries(scene.context, stale, 0, &good, 1) == mvfx_errorStale &&
            mvfxInstance_GetSceneQueries(NULL, scene.instance, 0, &queries, &count) ==
                mvfx_errorInvalid &&
            mvfxInstance_AnswerSceneQueries(NULL, scene.instance, 0, &good, 1) == mvfx_errorInvalid,
        "stale, no context");
    mvfxDestroyContext(scene.context);

    // An emitter without scene queries writes none.
    emitter.collision.sceneQueries = false;
    scene = NewScene(&emitter);
    Place(&scene, 0, 5, 0, 0, -4, 0);
    Step(&scene, 1);
    CHECK(Queries(&scene, &queries) == 0, "none without scene queries");
    mvfxDestroyContext(scene.context);

    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 5; ++field)
    {
        mvfxEmitterDef def = Asking(4);
        switch (field)
        {
        case 0:
            def.collision.queryCapacity = 0;
            break;
        case 1:
            def.collision.queryCapacity = MVFX_MAX_SCENE_QUERIES + 1;
            break;
        case 2:
            def.collision.lookahead = 0.0f;
            break;
        case 3:
            def.collision.lookahead = 1.5f;
            break;
        default:
            def.collision.lookahead = (float)NAN;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &def;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestQueryAndAnswer();
    TestRestAndCapacity();
    TestBirthInPlace();
    TestSpaces();
    TestAxesAndMotion();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
