// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles meeting each other: equal spheres trading their speeds
// at restitution 1, and moving on together at 0; spheres of eight times
// the mass bouncing as momentum says; bodies passing through each other
// without body contacts; three boxes stacked and still with their creep
// bounded; one large body among a thousand small ones examining a few
// cells each; a box landing aside slowed by every impulse of its
// impacts; a heavy box landing on a light one without rebound; a capsule lying
// across another; a box dropped on a box, and a sphere on a sphere
// rolling off; a box sliding to a stop on a wider
// one by friction, the one below held; a pile of spheres in a box settled
// without deep overlaps; the pool capped and counted; strikes counted on
// both bodies.

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

// Rigid bodies of a shape meeting each other and, with gravity, a plane
// at y = 0; eight substeps, no friction.
static mvfxEmitterDef Bodies(mvfxRigidShape shape, bool gravity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[0] = 0.0f;
    emitter.motion.gravity[1] = gravity ? -9.81f : 0.0f;
    emitter.motion.gravity[2] = 0.0f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1}, .max = {1, 1, 1}};
    emitter.rigid = (mvfxRigidDef){
        .enabled = true, .shape = shape, .density = 1.0f, .substeps = 8, .bodyContacts = 8};
    emitter.collision.enabled = true;
    emitter.colliders = gravity ? &s_ground : NULL;
    emitter.colliderCount = gravity ? 1 : 0;
    return emitter;
}

// Opens a scene of count bodies at places, with velocities and sizes
// (NULL for still and 1).
static void Open(const mvfxEmitterDef* emitter, const double* places, const float* velocities,
                 const float* sizes, int32_t count)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    static const float still[3 * 8] = {0};
    mvfxSpawnRequest request = {.count = count,
                                .positions = places,
                                .velocities = velocities != NULL ? velocities : still,
                                .sizes = sizes};
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

static float* Array(mvfxAttribute attribute)
{
    return mvfxAttributeArray(mvfxContextInstance(s_scene.context, s_scene.instance), 0, attribute);
}

// Body i's position or velocity along axis k, by its serial (its order of
// birth).
static int32_t IndexOf(uint32_t serial)
{
    const mvfxInstance* live = mvfxContextInstance(s_scene.context, s_scene.instance);
    const uint32_t* serials = (const uint32_t*)Array(mvfx_attributeSerial);
    for (int32_t i = 0; i < live->emitters[0].count; ++i)
    {
        if (serials[i] == serial)
        {
            return i;
        }
    }
    return 0;
}

static float X(uint32_t serial, int32_t k)
{
    return Array((mvfxAttribute)(mvfx_attributePositionX + k))[IndexOf(serial)];
}

static float V(uint32_t serial, int32_t k)
{
    return Array((mvfxAttribute)(mvfx_attributeVelocityX + k))[IndexOf(serial)];
}

static float Speed(uint32_t serial)
{
    return sqrtf(V(serial, 0) * V(serial, 0) + V(serial, 1) * V(serial, 1) +
                 V(serial, 2) * V(serial, 2));
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

static void TestHeadOn(void)
{
    // Spheres of 1 a metre and a half apart, the first at 2 toward the
    // other: at restitution 1 they trade speeds, at 0 they go on at 1.
    static const double places[6] = {-1, 0, 0, 0.5, 0, 0};
    static const float velocities[6] = {2, 0, 0, 0, 0, 0};
    for (int32_t elastic = 0; elastic < 2; ++elastic)
    {
        mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, false);
        emitter.rigid.restitution = elastic == 1 ? 1.0f : 0.0f;
        Open(&emitter, places, velocities, NULL, 2);
        Steps(60);
        if (elastic == 1)
        {
            CHECK(fabsf(V(0, 0)) <= 0.02f && fabsf(V(1, 0) - 2.0f) <= 0.02f, "traded speeds");
        }
        else
        {
            CHECK(fabsf(V(0, 0) - 1.0f) <= 0.02f && fabsf(V(1, 0) - 1.0f) <= 0.02f, "on together");
        }
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestMasses(void)
{
    // A sphere of 1 at 2 into one of 2, eight times its mass, at
    // restitution 1: it comes back at 7/9 of 2, the other goes at 2/9 of
    // 2.
    static const double places[6] = {-1.5, 0, 0, 0.5, 0, 0};
    static const float velocities[6] = {2, 0, 0, 0, 0, 0};
    static const float sizes[6] = {1, 1, 1, 2, 2, 2};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, false);
    emitter.rigid.restitution = 1.0f;
    Open(&emitter, places, velocities, sizes, 2);
    Steps(60);
    float momentum = V(0, 0) + 8.0f * V(1, 0);
    CHECK(fabsf(V(0, 0) + 14.0f / 9.0f) <= 0.04f && fabsf(V(1, 0) - 4.0f / 9.0f) <= 0.01f &&
              fabsf(momentum - 2.0f) <= 0.02f,
          "momentum kept");
    mvfxDestroyContext(s_scene.context);
}

static void TestThrough(void)
{
    // Without body contacts they pass through each other.
    static const double places[6] = {-1, 0, 0, 0.5, 0, 0};
    static const float velocities[6] = {2, 0, 0, 0, 0, 0};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, false);
    emitter.rigid.bodyContacts = 0;
    Open(&emitter, places, velocities, NULL, 2);
    Steps(60);
    CHECK(fabsf(V(0, 0) - 2.0f) <= 1e-4f && X(0, 0) > 0.9f && fabsf(V(1, 0)) <= 1e-6f,
          "through each other");
    mvfxDestroyContext(s_scene.context);
}

static void TestStack(void)
{
    // Three unit boxes stacked on the ground stay stacked and still, with
    // sleeping off: the solver's own creep (2.6 mm/s at the top, 6 mm
    // aside in 3 s when measured) bounded, so that sleeping never hides a
    // regression in it.
    static const double places[9] = {0, 0.5, 0, 0, 1.501, 0, 0, 2.502, 0};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox, true);
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    CHECK(emitter.rigid.sleepSpeed == 0.0f, "sleeping off");
    Open(&emitter, places, NULL, NULL, 3);
    Steps(180);
    bool still = true;
    for (uint32_t i = 0; i < 3; ++i)
    {
        float aside = sqrtf(X(i, 0) * X(i, 0) + X(i, 2) * X(i, 2));
        still = still && fabsf(X(i, 1) - (0.5f + (float)i)) <= 0.001f && aside <= 0.009f &&
                Speed(i) <= 0.004f;
    }
    CHECK(still, "a stack of three");
    mvfxDestroyContext(s_scene.context);
}

static void TestOneLarge(void)
{
    // A thousand small spheres (radius 0.05) 0.2 apart and one of radius
    // 3 beside them, no gravity, one substep a step: by its own level each looks
    // at a few cells, not at every body as one grid as wide as the large
    // one's reach would make it (about a million entries).
    enum
    {
        SMALL = 1000,
        ALL = SMALL + 1
    };
    static double places[3 * ALL];
    static float sizes[3 * ALL];
    static float still[3 * ALL];
    for (int32_t i = 0; i < SMALL; ++i)
    {
        places[3 * i] = 0.2 * (double)(i % 10);
        places[3 * i + 1] = 0.2 * (double)(i / 10 % 10);
        places[3 * i + 2] = 0.2 * (double)(i / 100);
        sizes[3 * i] = sizes[3 * i + 1] = sizes[3 * i + 2] = 0.1f;
    }
    places[3 * SMALL] = -4.0;
    places[3 * SMALL + 1] = 0.9;
    places[3 * SMALL + 2] = 0.9;
    sizes[3 * SMALL] = sizes[3 * SMALL + 1] = sizes[3 * SMALL + 2] = 6.0f;
    mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, false);
    emitter.capacity = ALL;
    emitter.hostSpawnCapacity = ALL;
    emitter.rigid.substeps = 1;
    Open(&emitter, places, still, sizes, ALL);
    // Born in the first step, they meet each other in the second.
    Steps(2);
    const mvfxInstance* live = mvfxContextInstance(s_scene.context, s_scene.instance);
    uint64_t examined = live->emitters[0].rigidExamined;
    CHECK(live->emitters[0].count == ALL && examined > 0u && examined <= 60u * ALL,
          "one large body among a thousand");
    mvfxDestroyContext(s_scene.context);
}

static void TestLandings(void)
{
    // A box landing at 3 aside from 0.3 up, restitution 0.5, friction 0.4:
    // each impact takes from its speed aside the friction times its normal
    // impulse, the pushes' and the restitution's (by hand: 0.74 m falling,
    // 0.38, 0.10 and 0.03 between bounces, little after), so it stops near
    // 1.27 m; by the pushes alone it went 2.8.
    static const float still[6] = {0};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox, true);
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.rigid.restitution = 0.5f;
    static const double high[3] = {0, 0.8, 0};
    static const float aside[3] = {3, 0, 0};
    Open(&emitter, high, aside, NULL, 1);
    Steps(180);
    CHECK(fabsf(X(0, 0) - 1.27f) <= 0.05f, "friction by every impulse of a landing");
    mvfxDestroyContext(s_scene.context);
    // A box of eight times the mass dropped 0.3 onto a unit box on the
    // ground at restitution 0: the restitution takes back what the pushes
    // gave too much, and neither rebounds faster than 0.08 (0.18 without).
    emitter.rigid.restitution = 0.0f;
    static const double stacked[6] = {0, 0.5, 0, 0, 2.3, 0};
    static const float sizes[6] = {1, 1, 1, 2, 2, 2};
    Open(&emitter, stacked, still, sizes, 2);
    float up = 0.0f;
    bool touched[2] = {false, false};
    for (int32_t i = 0; i < 120; ++i)
    {
        Steps(1);
        for (uint32_t b = 0; b < 2; ++b)
        {
            float v = V(b, 1);
            touched[b] = touched[b] || (i > 5 && v > -0.5f);
            up = touched[b] ? fmaxf(up, v) : up;
        }
    }
    CHECK(up <= 0.08f, "a heavy landing without rebound");
    mvfxDestroyContext(s_scene.context);
}

static void TestCapsules(void)
{
    // A capsule along z dropped across one along x lying on the ground
    // rests on it, its core a diameter above the other's.
    mvfxEmitterDef emitter = Bodies(mvfx_rigidCapsule, true);
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {1.5707963f, 0, 0}, .max = {1.5707963f, 0, 0}};
    static const float sizes[6] = {0.5f, 2, 0.5f, 0.5f, 2, 0.5f};
    static const double places[6] = {0, 0.25, 0, 0, 0.9, 0};
    Open(&emitter, places, NULL, sizes, 2);
    // The second turned along x by the host after its birth.
    Steps(1);
    float* q[4];
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] = Array((mvfxAttribute)(mvfx_attributeOrientationX + k));
    }
    int32_t lower = IndexOf(0);
    q[0][lower] = 0.0f;
    q[1][lower] = 0.0f;
    q[2][lower] = 0.70710678f;
    q[3][lower] = 0.70710678f;
    Steps(120);
    CHECK(fabsf(X(0, 1) - 0.25f) <= 0.01f && fabsf(X(1, 1) - 0.75f) <= 0.01f && Speed(1) <= 0.02f,
          "a capsule across a capsule");
    mvfxDestroyContext(s_scene.context);
}

static void TestDropped(void)
{
    // A box turned an eighth about y dropped onto a box on the ground
    // rests flat on it; a sphere dropped onto a sphere on the ground, a
    // little aside, rolls off it to the ground.
    static const double boxes[6] = {0, 0.5, 0, 0, 1.8, 0};
    mvfxEmitterDef box = Bodies(mvfx_rigidBox, true);
    box.rigid.staticFriction = 0.6f;
    box.rigid.dynamicFriction = 0.4f;
    box.orientationMode = mvfx_orientationEuler;
    box.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0, 0.785398f, 0}, .max = {0, 0.785398f, 0}};
    Open(&box, boxes, NULL, NULL, 2);
    Steps(120);
    CHECK(fabsf(X(0, 1) - 0.5f) <= 0.01f && fabsf(X(1, 1) - 1.5f) <= 0.01f && Speed(1) <= 0.02f,
          "a box on a box");
    mvfxDestroyContext(s_scene.context);
    static const double spheres[6] = {0, 0.5, 0, 0.1, 1.6, 0};
    mvfxEmitterDef ball = Bodies(mvfx_rigidSphere, true);
    ball.rigid.staticFriction = 0.6f;
    ball.rigid.dynamicFriction = 0.4f;
    Open(&ball, spheres, NULL, NULL, 2);
    Steps(180);
    CHECK(fabsf(X(1, 1) - 0.5f) <= 0.01f && fabsf(X(1, 0)) > 0.9f, "rolled off a sphere");
    mvfxDestroyContext(s_scene.context);
}

static void TestSliding(void)
{
    // A unit box sliding at 2 on a 3 by 1 by 3 box, dynamic friction 0.4:
    // it stops after 4 / (2 0.4 g), 0.51, the box under it held by the
    // ground.
    static const double places[6] = {0, 0.5, 0, -0.25, 1.5, 0};
    static const float velocities[6] = {0, 0, 0, 2, 0, 0};
    static const float sizes[6] = {3, 1, 3, 1, 1, 1};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox, true);
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    Open(&emitter, places, velocities, sizes, 2);
    Steps(90);
    float slid = X(1, 0) + 0.25f;
    CHECK(fabsf(slid - 0.51f) <= 0.05f && Speed(1) <= 0.02f && fabsf(X(0, 0)) <= 0.01f,
          "slid to a stop on a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestPile(void)
{
    // Eight spheres of radius 0.25 dropped in a column into a box 1.2 wide
    // settle in it, none more than 0.01 into another, all still.
    static const mvfxCollider box = {.kind = mvfx_colliderBox,
                                     .center = {0, 2, 0},
                                     .halfExtents = {0.6f, 2, 0.6f},
                                     .rotation = {0, 0, 0, 1},
                                     .inside = true};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, true);
    emitter.rigid.staticFriction = 0.5f;
    emitter.rigid.dynamicFriction = 0.3f;
    emitter.colliders = &box;
    emitter.colliderCount = 1;
    static const float sizes[24] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
                                    0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
                                    0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    double places[24];
    for (int32_t i = 0; i < 8; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 3) - 0.05;
        places[3 * i + 1] = 0.3 + 0.6 * (double)i;
        places[3 * i + 2] = 0.04 * (double)(i % 2) - 0.02;
    }
    Open(&emitter, places, NULL, sizes, 8);
    Steps(300);
    float deepest = 0.0f;
    float fastest = 0.0f;
    bool inside = true;
    for (uint32_t i = 0; i < 8; ++i)
    {
        fastest = fmaxf(fastest, Speed(i));
        inside = inside && fabsf(X(i, 0)) <= 0.36f && fabsf(X(i, 2)) <= 0.36f && X(i, 1) >= 0.24f;
        for (uint32_t j = i + 1; j < 8; ++j)
        {
            float d[3] = {X(i, 0) - X(j, 0), X(i, 1) - X(j, 1), X(i, 2) - X(j, 2)};
            deepest = fmaxf(deepest, 0.5f - sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
        }
    }
    CHECK(inside && deepest <= 0.01f && fastest <= 0.05f, "a pile in a box");
    mvfxDestroyContext(s_scene.context);
}

static void TestPoolAndStrikes(void)
{
    // Two unit boxes face to face with room for one contact a body: the
    // rest counted. Two spheres striking each other both count it.
    static const double faces[6] = {0, 0, 0, 0.99, 0, 0};
    mvfxEmitterDef boxes = Bodies(mvfx_rigidBox, false);
    boxes.rigid.bodyContacts = 1;
    Open(&boxes, faces, NULL, NULL, 2);
    Steps(2);
    CHECK(Stats().rigidContactsCapped > 0u, "the pool capped");
    mvfxDestroyContext(s_scene.context);
    static const double places[6] = {-1, 0, 0, 0.5, 0, 0};
    static const float velocities[6] = {2, 0, 0, 0, 0, 0};
    mvfxEmitterDef spheres = Bodies(mvfx_rigidSphere, false);
    spheres.rigid.restitution = 1.0f;
    Open(&spheres, places, velocities, NULL, 2);
    Steps(60);
    const uint32_t* count = (const uint32_t*)Array(mvfx_attributeCollisions);
    CHECK(count[IndexOf(0)] == 1u && count[IndexOf(1)] == 1u, "a strike on both");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestHeadOn();
    TestMasses();
    TestThrough();
    TestStack();
    TestOneLarge();
    TestLandings();
    TestCapsules();
    TestDropped();
    TestSliding();
    TestPile();
    TestPoolAndStrikes();
    return s_failures == 0 ? 0 : 1;
}
