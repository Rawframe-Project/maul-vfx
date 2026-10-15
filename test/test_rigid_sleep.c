// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sleeping rigid particles: a box at rest falls asleep, keeps its pose
// bit for bit and still dies at the end of its life; a sleeping stack
// stays asleep while a box falls past it and wakes as one when it lands,
// then sleeps again stacked; gravity turned by the instance wakes a box,
// which slides; moving ends a still time; a sleeper holds as ground
// under a heavier box landing on it; a box spinning in place stays
// awake; a host table set again unchanged leaves a box asleep, moved
// wakes it to fall; changed colliders wake only the bodies near them; a
// depth image set again wakes a box; the host wakes
// bodies within a sphere; no sleep speed, no sleeping; the checks.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene s_scene;

static const mvfxCollider s_ground = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};

// Unit boxes with gravity on the ground, sleeping at 0.05 after 0.5 s.
static mvfxEmitterDef Boxes(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1}, .max = {1, 1, 1}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 0}, .max = {0, 0, 0}};
    emitter.rigid.enabled = true;
    emitter.rigid.shape = mvfx_rigidBox;
    emitter.rigid.density = 1.0f;
    emitter.rigid.substeps = 8;
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.collision.enabled = true;
    emitter.colliders = &s_ground;
    emitter.colliderCount = 1;
    return emitter;
}

static void OpenContext(void)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
}

static mvfxResult EffectOf(const mvfxEmitterDef* emitter, mvfxEffectId* effect)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    return mvfxCreateEffect(s_scene.context, &def, effect);
}

static void Play(const mvfxEmitterDef* emitter)
{
    mvfxEffectId effect = {0};
    CHECK(EffectOf(emitter, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
}

static void Spawn(const double* places, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

// Particle i's attribute.
static float At(int32_t i, mvfxAttribute attribute)
{
    return ((const float*)mvfxAttributeArray(mvfxContextInstance(s_scene.context, s_scene.instance),
                                             0, attribute))[i];
}

// Particle i's place and orientation.
static void PoseOf(int32_t i, float out[7])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = At(i, (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out[3 + k] = At(i, (mvfxAttribute)(mvfx_attributeOrientationX + k));
    }
}

static void TestFallsAsleep(void)
{
    // A box dropped from 0.1 above the ground, living 3 s: asleep by 1.5
    // s, its pose the same bits half a second on with no velocity, and
    // gone after its life.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.lifetime = mvfxRangeSource(3.0f, 3.0f);
    Play(&boxes);
    static const double place[3] = {0, 0.6, 0};
    Spawn(place, 1);
    Steps(90);
    bool asleep = Stats().asleep == 1;
    float before[7];
    PoseOf(0, before);
    Steps(30);
    float after[7];
    PoseOf(0, after);
    bool held = memcmp(before, after, sizeof before) == 0 && At(0, mvfx_attributeVelocityY) == 0.0f;
    CHECK(asleep && held && fabsf(after[1] - 0.5f) <= 0.01f, "a box at rest falls asleep");
    Steps(70);
    CHECK(Stats().live == 0, "a sleeping box still dies");
    mvfxDestroyContext(s_scene.context);
}

static void TestPileWakes(void)
{
    // Two boxes stacked sleep; a third dropped from 3 m leaves them asleep
    // as it falls, wakes both as it lands, and the three sleep stacked.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.rigid.bodyContacts = 8;
    Play(&boxes);
    static const double stack[6] = {0, 0.5, 0, 0, 1.501, 0};
    Spawn(stack, 2);
    Steps(120);
    bool slept = Stats().asleep == 2;
    static const double above[3] = {0, 4.0, 0};
    Spawn(above, 1);
    Steps(1);
    bool passed = Stats().asleep == 2;
    bool woke = false;
    for (int32_t i = 0; i < 90 && !woke; ++i)
    {
        Steps(1);
        woke = Stats().asleep == 0;
    }
    CHECK(slept && passed && woke, "a landing box wakes the pile");
    Steps(240);
    bool stacked = Stats().asleep == 3;
    for (int32_t i = 0; stacked && i < 3; ++i)
    {
        uint32_t serial = ((const uint32_t*)mvfxAttributeArray(
            mvfxContextInstance(s_scene.context, s_scene.instance), 0, mvfx_attributeSerial))[i];
        stacked = fabsf(At(i, mvfx_attributePositionY) - (0.5f + (float)serial)) <= 0.02f;
    }
    CHECK(stacked, "the three sleep stacked");
    mvfxDestroyContext(s_scene.context);
}

static void TestForcesWake(void)
{
    // A box in the instance's space on its ground, gravity the world's:
    // asleep, then the instance tilted 40 degrees (past friction's 31) so
    // gravity turns in the box's space: it wakes and slides.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .space = mvfx_spaceLocal, .normal = {0, 1, 0}};
    boxes.space = mvfx_spaceLocal;
    boxes.motion.gravitySpace = mvfx_spaceWorld;
    boxes.colliders = &ground;
    Play(&boxes);
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Stats().asleep == 1;
    static const double origin[3] = {0, 0, 0};
    const float tilt[4] = {0, 0, sinf(0.3490659f), cosf(0.3490659f)};
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, tilt, 1.0f, true) ==
              mvfx_success,
          "tilted");
    Steps(1);
    bool woke = Stats().asleep == 0;
    Steps(30);
    CHECK(slept && woke && fabsf(At(0, mvfx_attributePositionX)) > 0.1f,
          "turned gravity wakes a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestStillResets(void)
{
    // A box still 0.4 s of a 2 s sleep time, then tilted to slide: its
    // still time starts again from 0.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .space = mvfx_spaceLocal, .normal = {0, 1, 0}};
    boxes.space = mvfx_spaceLocal;
    boxes.motion.gravitySpace = mvfx_spaceWorld;
    boxes.colliders = &ground;
    boxes.rigid.sleepTime = 2.0f;
    Play(&boxes);
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(24);
    bool still = At(0, mvfx_attributeSleep) >= 0.3f;
    static const double origin[3] = {0, 0, 0};
    const float tilt[4] = {0, 0, sinf(0.3490659f), cosf(0.3490659f)};
    CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, tilt, 1.0f, true) ==
              mvfx_success,
          "tilted");
    Steps(3);
    CHECK(still && At(0, mvfx_attributeSleep) == 0.0f, "moving ends a still time");
    mvfxDestroyContext(s_scene.context);
}

static void TestSleeperHolds(void)
{
    // A box twice the size (eight times the mass) dropped from 3 m onto a
    // sleeping box: the sleeper, meeting no ground while asleep, holds as
    // ground for the landing step instead of being driven into the floor.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.rigid.bodyContacts = 8;
    Play(&boxes);
    static const double low[3] = {0, 0.5, 0};
    static const float unit[3] = {1, 1, 1};
    mvfxSpawnRequest first = {.count = 1, .positions = low, .sizes = unit};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &first) == mvfx_success,
          "first");
    Steps(60);
    bool slept = Stats().asleep == 1;
    static const double high[3] = {0, 4.0, 0};
    static const float big[3] = {2, 2, 2};
    mvfxSpawnRequest second = {.count = 1, .positions = high, .sizes = big};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &second) == mvfx_success,
          "second");
    float lowest = INFINITY;
    for (int32_t i = 0; i < 90; ++i)
    {
        Steps(1);
        lowest = fminf(lowest, At(0, mvfx_attributePositionY));
    }
    CHECK(slept && lowest >= 0.499f, "a sleeper holds a landing");
    mvfxDestroyContext(s_scene.context);
}

// Far depth everywhere, from a camera at (0, 0, 10) looking down -Z.
static const float s_far[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// The emitter's sleeping bodies' still times: whether particle i sleeps.
static bool Asleep(int32_t i)
{
    return At(i, mvfx_attributeSleep) < 0.0f;
}

static void TestWorldNear(void)
{
    // Boxes asleep on two host tables 6 apart: a table lowered wakes the
    // box on it alone; a ball added far off wakes none; the other table
    // and the ball taken away wake the box that stood on it alone.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    mvfxCollider set[3] = {{.kind = mvfx_colliderBox,
                            .center = {-3, -0.5f, 0},
                            .halfExtents = {1, 0.5f, 1},
                            .rotation = {0, 0, 0, 1}},
                           {.kind = mvfx_colliderBox,
                            .center = {3, -0.5f, 0},
                            .halfExtents = {1, 0.5f, 1},
                            .rotation = {0, 0, 0, 1}},
                           {.kind = mvfx_colliderSphere, .center = {0, 3, 0}, .radius = 0.5f}};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 2) == mvfx_success,
          "tables");
    static const double places[6] = {-3, 0.5, 0, 3, 0.5, 0};
    Spawn(places, 2);
    Steps(60);
    bool slept = Asleep(0) && Asleep(1);
    set[0].center[1] = -0.8f;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 2) == mvfx_success,
          "one lowered");
    Steps(1);
    bool lowered = !Asleep(0) && Asleep(1);
    Steps(60);
    bool settled = Asleep(0) && Asleep(1);
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 3) == mvfx_success,
          "a ball");
    Steps(1);
    bool far = Asleep(0) && Asleep(1);
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, set, 1) == mvfx_success,
          "one table");
    Steps(1);
    bool removed = Asleep(0) && !Asleep(1);
    CHECK(slept && lowered && settled && far && removed, "the world wakes the bodies near it");
    mvfxDestroyContext(s_scene.context);
}

static void TestDepthWakes(void)
{
    // A box asleep on the ground by an empty depth image: the image set
    // again wakes it (its pixels may have changed).
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.collision.depthImage = true;
    boxes.collision.depthThickness = 0.5f;
    boxes.collision.depthSamples = 4;
    Play(&boxes);
    const float a = 100.0f / (0.1f - 100.0f);
    const mvfxDepthImage image = {
        .pixels = s_far,
        .byteCount = (int64_t)sizeof s_far,
        .width = 4,
        .height = 4,
        .stride = 16,
        .format = mvfx_depthFloat32,
        .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, -1, 0, 0, a * 0.1f, 0},
        .origin = {0, 0, 10},
        .rotation = {0, 0, 0, 1},
    };
    CHECK(mvfxInstance_SetDepthImage(s_scene.context, s_scene.instance, &image) == mvfx_success,
          "depth");
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Stats().asleep == 1;
    CHECK(mvfxInstance_SetDepthImage(s_scene.context, s_scene.instance, &image) == mvfx_success,
          "depth again");
    Steps(1);
    CHECK(slept && Stats().asleep == 0, "a depth image set again wakes a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestSpinning(void)
{
    // A box spinning in place at 1 radian a second with no gravity or
    // ground: its center still, its corners not, it never sleeps.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.motion.gravity[1] = 0.0f;
    boxes.collision.enabled = false;
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 1, 0}, .max = {0, 1, 0}};
    Play(&boxes);
    static const double place[3] = {0, 2, 0};
    Spawn(place, 1);
    Steps(120);
    CHECK(Stats().asleep == 0 && fabsf(At(0, mvfx_attributeAngularY) - 1.0f) <= 1e-3f,
          "a spinning box stays awake");
    mvfxDestroyContext(s_scene.context);
}

static void TestWorldWakes(void)
{
    // A box asleep on the host's table; the same table set again leaves it
    // asleep, the table set 0.3 lower wakes it, and it sleeps on it again.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.colliders = NULL;
    boxes.colliderCount = 0;
    boxes.collision.hostColliders = true;
    Play(&boxes);
    mvfxCollider table = {.kind = mvfx_colliderBox,
                          .center = {0, -0.5f, 0},
                          .halfExtents = {2, 0.5f, 2},
                          .rotation = {0, 0, 0, 1}};
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &table, 1) == mvfx_success,
          "table");
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(60);
    bool slept = Stats().asleep == 1;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &table, 1) == mvfx_success,
          "table again");
    Steps(1);
    bool kept = Stats().asleep == 1;
    table.center[1] = -0.8f;
    CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &table, 1) == mvfx_success,
          "table lowered");
    Steps(1);
    bool woke = Stats().asleep == 0;
    Steps(90);
    CHECK(slept && kept && woke && Stats().asleep == 1 &&
              fabsf(At(0, mvfx_attributePositionY) - 0.2f) <= 0.01f,
          "a moved table wakes a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestWakeWithin(void)
{
    // Two boxes asleep 6 apart: a sphere about one wakes it alone, an
    // infinite one the other; arguments out of range refused.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    Play(&boxes);
    static const double places[6] = {-3, 0.5, 0, 3, 0.5, 0};
    Spawn(places, 2);
    Steps(60);
    bool slept = Stats().asleep == 2;
    static const float near[3] = {-3.5f, 0.5f, 0};
    CHECK(mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, 0.1f) == mvfx_success,
          "near one");
    bool one =
        Stats().asleep == 1 &&
        ((const float*)mvfxAttributeArray(mvfxContextInstance(s_scene.context, s_scene.instance), 0,
                                          mvfx_attributeSleep))[0] == 0.0f;
    CHECK(mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, INFINITY) ==
              mvfx_success,
          "all");
    bool all = Stats().asleep == 0;
    const float nowhere[3] = {NAN, 0, 0};
    bool refused =
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, nowhere, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, -1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, near, NAN) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 1, near, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(s_scene.context, s_scene.instance, 0, NULL, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_WakeBodies(NULL, s_scene.instance, 0, near, 1.0f) == mvfx_errorInvalid;
    CHECK(slept && one && all && refused, "woken within a sphere");
    mvfxDestroyContext(s_scene.context);
}

static void TestNeverSleeps(void)
{
    // No sleep speed: a box at rest never sleeps, and the emitter keeps no
    // sleep state.
    OpenContext();
    mvfxEmitterDef boxes = Boxes();
    boxes.rigid.sleepSpeed = 0.0f;
    boxes.rigid.sleepTime = 0.0f;
    Play(&boxes);
    static const double place[3] = {0, 0.5, 0};
    Spawn(place, 1);
    Steps(120);
    const mvfxInstance* live = mvfxContextInstance(s_scene.context, s_scene.instance);
    CHECK(Stats().asleep == 0 &&
              !mvfxHasAttribute(&live->effect->emitters[0].plan, mvfx_attributeSleep),
          "no sleep speed, no sleeping");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    // The default's 0.05 and 0.5; a sleep speed below 0 or NaN, or one
    // above 0 with no sleep time or past the most, refused.
    OpenContext();
    mvfxEmitterDef def = mvfxDefaultEmitterDef();
    CHECK(def.rigid.sleepSpeed == 0.05f && def.rigid.sleepTime == 0.5f, "defaults");
    static const float speeds[5] = {-0.1f, NAN, 0.05f, 0.05f, 0.05f};
    static const float times[5] = {0.5f, 0.5f, 0.0f, 4000.0f, NAN};
    int32_t refused = 0;
    for (int32_t i = 0; i < 5; ++i)
    {
        mvfxEmitterDef boxes = Boxes();
        boxes.rigid.sleepSpeed = speeds[i];
        boxes.rigid.sleepTime = times[i];
        mvfxEffectId effect = {0};
        refused += EffectOf(&boxes, &effect) == mvfx_errorInvalid ? 1 : 0;
    }
    mvfxEmitterDef off = Boxes();
    off.rigid.sleepSpeed = 0.0f;
    off.rigid.sleepTime = 0.0f;
    mvfxEffectId effect = {0};
    CHECK(refused == 5 && EffectOf(&off, &effect) == mvfx_success, "the checks");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestFallsAsleep();
    TestPileWakes();
    TestForcesWake();
    TestStillResets();
    TestSleeperHolds();
    TestSpinning();
    TestWorldWakes();
    TestWorldNear();
    TestDepthWakes();
    TestWakeWithin();
    TestNeverSleeps();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
