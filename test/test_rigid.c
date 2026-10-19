// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles: a sphere dropped on a plane coming to rest on it; a
// bounce at its restitution; a sliding sphere starting to roll at five
// sevenths of its speed (and sliding on without friction); a box dropped
// on an edge toppling onto a face; a box held on a slope by static
// friction and sliding down a steeper one; bodies kept inside and outside
// spheres; a capsule lying down; collisions counted only as strikes, and
// killing; contacts capped and counted, the deepest kept; a box pushed
// off a sphere inside it through its nearest face; several strikes a step
// writing one event on the surface; a box spun about its middle axis
// flipping by the gyroscopic term while its momentum and energy hold; a
// box spun on a plane stopped by its friction at the rate its contacts'
// spread gives; a tall box tipping over on a slope a short one stands on;
// a box tipping at its edge's angle, standing steady parked at it and
// still below it, its creep near it bounded, and turned on its upright
// tipping over a corner at the diagonal's;
// the checks.

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

static mvfxCollider s_colliders[2];

// A rigid body of a shape and size, one substep in eight, a plane at y = 0.
static mvfxEmitterDef Rigid(mvfxRigidShape shape, float x, float y, float z)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[0] = 0.0f;
    emitter.motion.gravity[1] = -9.81f;
    emitter.motion.gravity[2] = 0.0f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {x, y, z}, .max = {x, y, z}};
    emitter.rigid = (mvfxRigidDef){.enabled = true,
                                   .shape = shape,
                                   .density = 1.0f,
                                   .substeps = 8,
                                   .staticFriction = 0.6f,
                                   .dynamicFriction = 0.4f};
    s_colliders[0] = (mvfxCollider){.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    emitter.collision.enabled = true;
    emitter.colliders = s_colliders;
    emitter.colliderCount = 1;
    return emitter;
}

// Turned by angle about z.
static void Tilt(mvfxEmitterDef* emitter, float angle)
{
    emitter->orientationMode = mvfx_orientationEuler;
    emitter->orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, angle}, .max = {0, 0, angle}};
}

static void Open(const mvfxEmitterDef* emitter, const double place[3], const float velocity[3])
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
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
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

static float X(int32_t k)
{
    return Array((mvfxAttribute)(mvfx_attributePositionX + k))[0];
}

static float V(int32_t k)
{
    return Array((mvfxAttribute)(mvfx_attributeVelocityX + k))[0];
}

static float W(int32_t k)
{
    return Array((mvfxAttribute)(mvfx_attributeAngularX + k))[0];
}

static void Q(float out[4])
{
    for (int32_t k = 0; k < 4; ++k)
    {
        out[k] = Array((mvfxAttribute)(mvfx_attributeOrientationX + k))[0];
    }
}

// The body's axis k in the world.
static void Axis(int32_t k, float out[3])
{
    float q[4];
    Q(q);
    float e[3] = {0.0f, 0.0f, 0.0f};
    e[k] = 1.0f;
    float t[3] = {2.0f * (q[1] * e[2] - q[2] * e[1]), 2.0f * (q[2] * e[0] - q[0] * e[2]),
                  2.0f * (q[0] * e[1] - q[1] * e[0])};
    out[0] = e[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = e[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = e[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

static float Speed(void)
{
    return sqrtf(V(0) * V(0) + V(1) * V(1) + V(2) * V(2));
}

static void TestRest(void)
{
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
    static const double place[3] = {0, 2, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(180);
    CHECK(fabsf(X(1) - 0.5f) <= 0.005f && Speed() <= 0.02f, "a sphere at rest on a plane");
    mvfxDestroyContext(s_scene.context);
}

static void TestBounce(void)
{
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
    emitter.motion.gravity[1] = 0.0f;
    emitter.rigid.restitution = 0.5f;
    static const double place[3] = {0, 0.6, 0};
    static const float down[3] = {0, -4, 0};
    Open(&emitter, place, down);
    Steps(10);
    CHECK(fabsf(V(1) - 2.0f) <= 0.02f && X(1) > 0.5f, "bounced at half the speed");
    mvfxDestroyContext(s_scene.context);
}

static void TestRoll(void)
{
    for (int32_t rough = 0; rough < 2; ++rough)
    {
        mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
        if (rough == 0)
        {
            emitter.rigid.staticFriction = 0.0f;
            emitter.rigid.dynamicFriction = 0.0f;
        }
        static const double place[3] = {0, 0.5, 0};
        static const float slide[3] = {3, 0, 0};
        Open(&emitter, place, slide);
        Steps(120);
        if (rough == 1)
        {
            // Rolling: its contact still, at five sevenths of the speed.
            CHECK(fabsf(V(0) - 3.0f * 5.0f / 7.0f) <= 0.05f && fabsf(V(0) + 0.5f * W(2)) <= 0.02f,
                  "rolling");
        }
        else
        {
            CHECK(fabsf(V(0) - 3.0f) <= 1e-4f && fabsf(W(2)) <= 1e-4f, "sliding without friction");
        }
        mvfxDestroyContext(s_scene.context);
    }
}

// The largest share of a body axis along y.
static float Upright(void)
{
    float most = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float axis[3];
        Axis(k, axis);
        most = fmaxf(most, fabsf(axis[1]));
    }
    return most;
}

static void TestTopple(void)
{
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
    Tilt(&emitter, 0.6f);
    static const double place[3] = {0, 1.5, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(240);
    CHECK(fabsf(X(1) - 0.5f) <= 0.01f && Upright() >= 0.9999f && Speed() <= 0.02f,
          "toppled onto a face");
    mvfxDestroyContext(s_scene.context);
}

static void TestSlope(void)
{
    // tan 0.3 is 0.31, held by 0.6; tan 0.7 is 0.84, not.
    for (int32_t steep = 0; steep < 2; ++steep)
    {
        float angle = steep == 1 ? 0.7f : 0.3f;
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
        Tilt(&emitter, angle);
        s_colliders[0].normal[0] = -sinf(angle);
        s_colliders[0].normal[1] = cosf(angle);
        double place[3] = {-0.5 * sin((double)angle), 0.5 * cos((double)angle), 0};
        static const float still[3] = {0, 0, 0};
        Open(&emitter, place, still);
        Steps(60);
        double moved = sqrt(((double)X(0) - place[0]) * ((double)X(0) - place[0]) +
                            ((double)X(1) - place[1]) * ((double)X(1) - place[1]));
        CHECK(steep == 1 ? moved > 0.5 : moved < 0.005, steep == 1 ? "slid" : "held");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestSpheres(void)
{
    // A sphere thrown about inside a sphere of radius 2, no gravity.
    mvfxEmitterDef inside = Rigid(mvfx_rigidSphere, 1, 1, 1);
    inside.motion.gravity[1] = 0.0f;
    inside.rigid.restitution = 1.0f;
    s_colliders[0] = (mvfxCollider){.kind = mvfx_colliderSphere, .radius = 2.0f, .inside = true};
    static const double center[3] = {0, 0, 0};
    static const float thrown[3] = {5, 3, 1};
    Open(&inside, center, thrown);
    float worst = 0.0f;
    for (int32_t i = 0; i < 120; ++i)
    {
        Steps(1);
        worst = fmaxf(worst, sqrtf(X(0) * X(0) + X(1) * X(1) + X(2) * X(2)) + 0.5f - 2.0f);
    }
    CHECK(worst <= 0.02f && Speed() >= 4.0f, "kept inside");
    mvfxDestroyContext(s_scene.context);
    // A box dropped on top of a sphere of radius 1 rests on its top.
    mvfxEmitterDef outside = Rigid(mvfx_rigidBox, 1, 1, 1);
    s_colliders[0] = (mvfxCollider){.kind = mvfx_colliderSphere, .radius = 1.0f};
    static const double above[3] = {0, 3, 0};
    static const float still[3] = {0, 0, 0};
    Open(&outside, above, still);
    Steps(120);
    CHECK(fabsf(X(1) - 1.5f) <= 0.01f && Speed() <= 0.02f, "resting on a sphere");
    mvfxDestroyContext(s_scene.context);
}

static void TestCapsule(void)
{
    mvfxEmitterDef emitter = Rigid(mvfx_rigidCapsule, 0.5f, 2.0f, 0.5f);
    Tilt(&emitter, 0.3f);
    static const double place[3] = {0, 1.5, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(300);
    float axis[3];
    Axis(1, axis);
    CHECK(fabsf(axis[1]) <= 0.01f && fabsf(X(1) - 0.25f) <= 0.01f, "lying down");
    mvfxDestroyContext(s_scene.context);
}

static void TestStrikes(void)
{
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
    static const double place[3] = {0, 2, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(120);
    const uint32_t* count = (const uint32_t*)Array(mvfx_attributeCollisions);
    uint32_t landed = count[0];
    Steps(60);
    CHECK(landed >= 1 && count[0] == landed, "strikes counted, not resting");
    mvfxDestroyContext(s_scene.context);
    emitter.collision.killOnCollision = true;
    Open(&emitter, place, still);
    Steps(60);
    CHECK(Stats().live == 0 && Stats().killed == 1, "killed by a strike");
    mvfxDestroyContext(s_scene.context);
}

static void TestCapped(void)
{
    // A box wholly behind two planes: sixteen contacts, eight kept.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
    s_colliders[1] = (mvfxCollider){.kind = mvfx_colliderPlane, .normal = {1, 0, 0}};
    emitter.colliderCount = 2;
    static const double place[3] = {-1, -1, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(1);
    CHECK(Stats().rigidContactsCapped >= 8u, "capped and counted");
    mvfxDestroyContext(s_scene.context);
}

static void TestDeepest(void)
{
    // A box 0.01 into a floor, 0.05 into a wall along z and 0.5 into one
    // along x, in one substep and no gravity: twelve contacts, the eight
    // deepest kept, so it comes out of the walls and not yet the floor.
    static const mvfxCollider walls[3] = {
        {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderPlane, .normal = {1, 0, 0}},
        {.kind = mvfx_colliderPlane, .normal = {0, 0, 1}},
    };
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
    emitter.motion.gravity[1] = 0.0f;
    emitter.rigid.substeps = 1;
    emitter.colliders = walls;
    emitter.colliderCount = 3;
    static const double place[3] = {0, 0.49, 0.45};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(1);
    CHECK(fabsf(X(1) - 0.49f) <= 1e-6f && X(2) >= 0.495f && X(0) >= 0.5f, "the deepest kept");
    mvfxDestroyContext(s_scene.context);
}

static void TestInsideBox(void)
{
    // A small sphere at the origin inside a box of 2 centered 0.7 above
    // it: pushed out through the nearest face, the bottom, to rest just
    // above the sphere.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 2, 2, 2);
    emitter.motion.gravity[1] = 0.0f;
    s_colliders[0] = (mvfxCollider){.kind = mvfx_colliderSphere, .radius = 0.1f};
    static const double place[3] = {0, 0.7, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(10);
    CHECK(fabsf(X(1) - 1.1f) <= 0.01f && fabsf(X(0)) <= 1e-4f && Speed() <= 1e-3f,
          "out by its nearest face");
    mvfxDestroyContext(s_scene.context);
}

static void TestStrikeEvents(void)
{
    // A ball bouncing fast between planes 0.2 apart strikes several times a
    // step but writes one event, on a surface.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
    emitter.motion.gravity[1] = 0.0f;
    emitter.rigid.restitution = 1.0f;
    emitter.events = mvfx_eventMaskCollision;
    emitter.eventCapacity = 16;
    s_colliders[1] =
        (mvfxCollider){.kind = mvfx_colliderPlane, .center = {0, 1.2f, 0}, .normal = {0, -1, 0}};
    emitter.colliderCount = 2;
    static const double place[3] = {0, 0.6, 0};
    static const float fast[3] = {0, 60, 0};
    Open(&emitter, place, fast);
    Steps(2);
    const uint32_t* count = (const uint32_t*)Array(mvfx_attributeCollisions);
    uint32_t before = count[0];
    Steps(1);
    const mvfxEvent* events = NULL;
    int32_t n = 0;
    CHECK(mvfxInstance_GetEvents(s_scene.context, s_scene.instance, 0, &events, &n) == mvfx_success,
          "events");
    bool onSurface =
        n == 1 && (fabs(events[0].place[1]) <= 1e-4 || fabs(events[0].place[1] - 1.2) <= 1e-4);
    CHECK(count[0] >= before + 2u && onSurface, "strikes counted, one event a step");
    mvfxDestroyContext(s_scene.context);
}

static void TestHeldStrikes(void)
{
    // Two spheres striking the floor in their second step, the second
    // first: their events come body by body after the substeps, each at
    // its strike.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
    emitter.motion.gravity[1] = 0.0f;
    emitter.events = mvfx_eventMaskCollision;
    emitter.eventCapacity = 16;
    static const double places[6] = {-2, 0.59, 0, 2, 0.555, 0};
    static const float velocities[6] = {0, -3, 0, 0, -3, 0};
    Open(&emitter, places, velocities);
    mvfxSpawnRequest second = {.count = 1, .positions = places + 3, .velocities = velocities + 3};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &second) == mvfx_success,
          "the second spawned");
    Steps(2);
    const mvfxEvent* events = NULL;
    int32_t n = 0;
    CHECK(mvfxInstance_GetEvents(s_scene.context, s_scene.instance, 0, &events, &n) ==
                  mvfx_success &&
              n == 2 && events[0].serial < events[1].serial && events[0].tick > events[1].tick,
          "strikes' events body by body");
    mvfxDestroyContext(s_scene.context);
}

// A box collider of half sizes about center, turned by angle about axis
// x (0) or z (2).
static mvfxCollider Box(float x, float y, float z, const float center[3], int32_t axis, float angle)
{
    mvfxCollider box = {.kind = mvfx_colliderBox,
                        .center = {center[0], center[1], center[2]},
                        .halfExtents = {x, y, z},
                        .rotation = {0, 0, 0, 1}};
    box.rotation[axis] = sinf(0.5f * angle);
    box.rotation[3] = cosf(0.5f * angle);
    return box;
}

// The lowest y of the unit body box.
static float Lowest(void)
{
    float lowest = INFINITY;
    for (int32_t i = 0; i < 8; ++i)
    {
        float y = X(1);
        for (int32_t k = 0; k < 3; ++k)
        {
            float axis[3];
            Axis(k, axis);
            y += ((i >> k & 1) != 0 ? 0.5f : -0.5f) * axis[1];
        }
        lowest = fminf(lowest, y);
    }
    return lowest;
}

static void TestTable(void)
{
    // A box on a table of 4 by 1 by 4 rests flat on it; one hanging past
    // its edge by more than half tips off it.
    static const float origin[3] = {0, 0, 0};
    for (int32_t over = 0; over < 2; ++over)
    {
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
        s_colliders[0] = Box(2, 0.5f, 2, origin, 0, 0.0f);
        double place[3] = {over == 1 ? 2.2 : 0.5, 1.5, 0};
        static const float still[3] = {0, 0, 0};
        Open(&emitter, place, still);
        Steps(over == 1 ? 120 : 90);
        if (over == 1)
        {
            CHECK(X(1) < 0.0f, "tipped off the edge");
        }
        else
        {
            CHECK(fabsf(X(1) - 1.0f) <= 0.01f && Upright() >= 0.9999f && Speed() <= 0.02f &&
                      fabsf(X(0) - 0.5f) <= 0.01f,
                  "resting on a table");
        }
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestEdges(void)
{
    // A beam along x turned onto an edge, and a box turned onto an edge
    // along z dropped across it: edge on edge, never through it.
    static const float origin[3] = {0, 0, 0};
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
    emitter.rigid.staticFriction = 0.0f;
    emitter.rigid.dynamicFriction = 0.0f;
    Tilt(&emitter, 0.785398f);
    s_colliders[0] = Box(3, 0.2f, 0.2f, origin, 0, 0.785398f);
    static const double place[3] = {0, 1.2, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    float top = 0.2f * 1.41421356f;
    float worst = INFINITY;
    for (int32_t i = 0; i < 20; ++i)
    {
        Steps(1);
        worst = fminf(worst, Lowest() - top);
    }
    CHECK(worst >= -0.02f && X(1) < 1.1f, "edge on edge");
    mvfxDestroyContext(s_scene.context);
}

static void TestBeam(void)
{
    // A capsule of length 2 lying across a block 0.6 wide rests on it, its
    // ends in the air; a sphere rests on a beam 0.2 wide.
    static const float origin[3] = {0, 0, 0};
    for (int32_t sphere = 0; sphere < 2; ++sphere)
    {
        mvfxEmitterDef emitter = sphere == 1 ? Rigid(mvfx_rigidSphere, 0.5f, 0.5f, 0.5f)
                                             : Rigid(mvfx_rigidCapsule, 0.5f, 2.0f, 0.5f);
        Tilt(&emitter, 1.5707963f);
        s_colliders[0] = Box(sphere == 1 ? 0.1f : 0.3f, 0.1f, 3, origin, 0, 0.0f);
        static const double place[3] = {0, 0.6, 0};
        static const float still[3] = {0, 0, 0};
        Open(&emitter, place, still);
        Steps(90);
        CHECK(fabsf(X(1) - 0.35f) <= 0.01f && Speed() <= 0.02f,
              sphere == 1 ? "a sphere on a beam" : "a capsule across a beam");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestInsideBoxes(void)
{
    // A box thrown about inside a box of 3 by 2 by 2 turned about z,
    // without gravity or friction, bouncing whole: every corner kept
    // inside, its energy never above what it was thrown with (corners
    // struck at once share the bounce) and most of it kept. The same box as
    // the host's collider.
    static const float origin[3] = {0, 0, 0};
    for (int32_t host = 0; host < 2; ++host)
    {
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
        emitter.motion.gravity[1] = 0.0f;
        emitter.rigid.restitution = 1.0f;
        emitter.rigid.staticFriction = 0.0f;
        emitter.rigid.dynamicFriction = 0.0f;
        mvfxCollider room = Box(1.5f, 1, 1, origin, 2, 0.4f);
        room.inside = true;
        s_colliders[0] = room;
        if (host == 1)
        {
            emitter.colliders = NULL;
            emitter.colliderCount = 0;
            emitter.collision.hostColliders = true;
        }
        static const double place[3] = {0, 0, 0};
        static const float thrown[3] = {4, 3, 2};
        Open(&emitter, place, thrown);
        if (host == 1)
        {
            CHECK(mvfxInstance_SetColliders(s_scene.context, s_scene.instance, &room, 1) ==
                      mvfx_success,
                  "host collider");
        }
        float worst = 0.0f;
        float most = 0.0f;
        float energy = 0.0f;
        for (int32_t i = 0; i < 120; ++i)
        {
            Steps(1);
            // A unit cube's, over its mass: its inertia a sixth.
            energy = 0.5f * Speed() * Speed() + (W(0) * W(0) + W(1) * W(1) + W(2) * W(2)) / 12.0f;
            most = fmaxf(most, energy);
            for (int32_t c = 0; c < 8; ++c)
            {
                float corner[3] = {X(0), X(1), X(2)};
                for (int32_t k = 0; k < 3; ++k)
                {
                    float axis[3];
                    Axis(k, axis);
                    for (int32_t j = 0; j < 3; ++j)
                    {
                        corner[j] += ((c >> k & 1) != 0 ? 0.5f : -0.5f) * axis[j];
                    }
                }
                float s = sinf(-0.4f);
                float co = cosf(-0.4f);
                float local[3] = {co * corner[0] - s * corner[1], s * corner[0] + co * corner[1],
                                  corner[2]};
                worst = fmaxf(worst, fmaxf(fabsf(local[0]) - 1.5f,
                                           fmaxf(fabsf(local[1]) - 1.0f, fabsf(local[2]) - 1.0f)));
            }
        }
        CHECK(worst <= 0.02f && most <= 14.5f * 1.02f && energy >= 14.5f * 0.85f,
              host == 1 ? "a host's box" : "kept in a box, its energy kept");
        mvfxDestroyContext(s_scene.context);
    }
}

// The body's angular momentum and twice its energy over its mass.
static void MomentumOf(const float inertia[3], float* momentum, float* energy)
{
    float q[4];
    Q(q);
    float w[3] = {W(0), W(1), W(2)};
    float local[3] = {0, 0, 0};
    for (int32_t k = 0; k < 3; ++k)
    {
        float axis[3];
        Axis(k, axis);
        local[k] = axis[0] * w[0] + axis[1] * w[1] + axis[2] * w[2];
    }
    *momentum = sqrtf(powf(inertia[0] * local[0], 2.0f) + powf(inertia[1] * local[1], 2.0f) +
                      powf(inertia[2] * local[2], 2.0f));
    *energy = inertia[0] * local[0] * local[0] + inertia[1] * local[1] * local[1] +
              inertia[2] * local[2] * local[2];
}

static void TestGyroscopic(void)
{
    // A 1 by 2 by 3 box spun about its middle axis (Y) flips over.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 2, 3);
    emitter.motion.gravity[1] = 0.0f;
    emitter.collision.enabled = false;
    emitter.colliders = NULL;
    emitter.colliderCount = 0;
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0.05f, 4, 0}, .max = {0.05f, 4, 0}};
    static const double place[3] = {0, 0, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(1);
    const float inertia[3] = {13.0f / 12.0f, 10.0f / 12.0f, 5.0f / 12.0f};
    float momentum = 0.0f;
    float energy = 0.0f;
    MomentumOf(inertia, &momentum, &energy);
    bool flipped = false;
    float drift = 0.0f;
    for (int32_t i = 0; i < 300; ++i)
    {
        Steps(1);
        float axis[3];
        Axis(1, axis);
        flipped = flipped || axis[1] < -0.5f;
        float m = 0.0f;
        float e = 0.0f;
        MomentumOf(inertia, &m, &e);
        drift = fmaxf(drift, fmaxf(fabsf(m / momentum - 1.0f), fabsf(e / energy - 1.0f)));
    }
    CHECK(flipped, "flipped about its middle axis");
    CHECK(drift <= 0.02f, "momentum and energy held");
    mvfxDestroyContext(s_scene.context);
}

static bool s_bump;

// A grid of heights over x and z from -2 to 2, three points a side:
// flat, half of x, or a bump of 0.3 at the middle.
static mvfxCollider Heights(mvfxContext* context, bool slope)
{
    static const float flat[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const float rising[9] = {-1, 0, 1, -1, 0, 1, -1, 0, 1};
    static const float bump[9] = {0, 0, 0, 0, 0.3f, 0, 0, 0, 0};
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 3;
    def.size[2] = 3;
    def.values = slope ? rising : (s_bump ? bump : flat);
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "heights");
    return (mvfxCollider){.kind = mvfx_colliderHeightfield,
                          .halfExtents = {2, 0, 2},
                          .rotation = {0, 0, 0, 1},
                          .grid = grid};
}

// The distance to a sphere of radius 1, over a box of half size 2, 17
// points a side.
static mvfxCollider Ball(mvfxContext* context)
{
    static float distances[17 * 17 * 17];
    for (int32_t i = 0; i < 17 * 17 * 17; ++i)
    {
        float p[3] = {(float)(i % 17) * 0.25f - 2.0f, (float)(i / 17 % 17) * 0.25f - 2.0f,
                      (float)(i / 289) * 0.25f - 2.0f};
        distances[i] = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - 1.0f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 17;
    def.size[1] = 17;
    def.size[2] = 17;
    def.values = distances;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "ball");
    return (mvfxCollider){.kind = mvfx_colliderDistance,
                          .halfExtents = {2, 2, 2},
                          .rotation = {0, 0, 0, 1},
                          .grid = grid};
}

// Opens a scene whose emitter meets one collider the context makes.
static void OpenOn(mvfxEmitterDef* emitter, mvfxCollider (*make)(mvfxContext*, bool), bool flag,
                   const double place[3], const float velocity[3])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = nullptr;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    s_colliders[0] = make(context, flag);
    emitter->colliderCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memset(&s_scene, 0, sizeof s_scene);
    s_scene.context = context;
    CHECK(mvfxCreateInstance(context, &instanceDef, &s_scene.instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(context, s_scene.instance, 0, &request) == mvfx_success, "spawned");
}

static mvfxCollider BallOf(mvfxContext* context, bool unused)
{
    (void)unused;
    return Ball(context);
}

static void TestGrids(void)
{
    static const float still[3] = {0, 0, 0};
    // A box on flat heights rests flat on them.
    mvfxEmitterDef box = Rigid(mvfx_rigidBox, 1, 1, 1);
    static const double above[3] = {0.3, 1.5, 0.2};
    OpenOn(&box, Heights, false, above, still);
    Steps(90);
    CHECK(fabsf(X(1) - 0.5f) <= 0.01f && Upright() >= 0.9999f && Speed() <= 0.02f,
          "a box on a heightfield");
    mvfxDestroyContext(s_scene.context);
    // A box over a bump rests on its top (by its bottom face's center,
    // the corners in the air), not on its corners.
    s_bump = true;
    mvfxEmitterDef perched = Rigid(mvfx_rigidBox, 1, 1, 1);
    static const double over[3] = {0, 0.81, 0};
    OpenOn(&perched, Heights, false, over, still);
    Steps(20);
    CHECK(X(1) >= 0.79f, "a box on a bump");
    mvfxDestroyContext(s_scene.context);
    s_bump = false;
    // A box on heights rising at a half, turned to lie on them, is held
    // by friction 0.6, its center 0.5 across the slope from it.
    float angle = atanf(0.5f);
    mvfxEmitterDef held = Rigid(mvfx_rigidBox, 1, 1, 1);
    Tilt(&held, angle);
    double on[3] = {-0.5 * sin((double)angle), 0.5 * cos((double)angle), 0};
    OpenOn(&held, Heights, true, on, still);
    Steps(60);
    float across = (X(1) - 0.5f * X(0)) * cosf(angle);
    CHECK(fabsf(X(0) - (float)on[0]) <= 0.01f && fabsf(across - 0.5f) <= 0.01f,
          "held on a sloped heightfield");
    mvfxDestroyContext(s_scene.context);
    // A sphere on a ball of distances rests on its top.
    mvfxEmitterDef sphere = Rigid(mvfx_rigidSphere, 0.5f, 0.5f, 0.5f);
    static const double atop[3] = {0, 1.8, 0};
    OpenOn(&sphere, BallOf, false, atop, still);
    Steps(90);
    CHECK(fabsf(X(1) - 1.25f) <= 0.02f && fabsf(X(0)) <= 1e-3f && Speed() <= 0.02f,
          "a sphere on a distance grid");
    mvfxDestroyContext(s_scene.context);
}

static void TestSceneQueries(void)
{
    // A box falling down z with no colliders of its own, the host
    // answering each query with the ground at z = 0: it rests on it.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.gravity[2] = -9.81f;
    emitter.colliders = NULL;
    emitter.colliderCount = 0;
    emitter.collision.sceneQueries = true;
    emitter.collision.queryCapacity = 4;
    emitter.collision.lookahead = 0.25f;
    static const double above[3] = {0, 0, 1.5};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, above, still);
    for (int32_t i = 0; i < 90; ++i)
    {
        Steps(1);
        const mvfxSceneQuery* queries = NULL;
        int32_t count = 0;
        CHECK(mvfxInstance_GetSceneQueries(s_scene.context, s_scene.instance, 0, &queries,
                                           &count) == mvfx_success,
              "queries");
        static const mvfxSceneAnswer ground[4] = {{.hit = true, .normal = {0, 0, 1}},
                                                  {.hit = true, .normal = {0, 0, 1}},
                                                  {.hit = true, .normal = {0, 0, 1}},
                                                  {.hit = true, .normal = {0, 0, 1}}};
        CHECK(mvfxInstance_AnswerSceneQueries(s_scene.context, s_scene.instance, 0, ground,
                                              count) == mvfx_success,
              "answered");
    }
    CHECK(fabsf(X(2) - 0.5f) <= 0.01f && Speed() <= 0.02f, "resting on the host's ground");
    mvfxDestroyContext(s_scene.context);
}

// A camera 6 along z looking down -z at a floor at z = 0, 90 degrees
// wide, 32 pixels a side.
static float s_depth[32 * 32];

static mvfxDepthImage Floor(void)
{
    const float a = (float)(100.0 / (0.1 - 100.0));
    for (int32_t i = 0; i < 32 * 32; ++i)
    {
        s_depth[i] = (a * -6.0f + a * 0.1f) / 6.0f;
    }
    return (mvfxDepthImage){.pixels = s_depth,
                            .byteCount = (int64_t)sizeof s_depth,
                            .width = 32,
                            .height = 32,
                            .stride = 32 * 4,
                            .format = mvfx_depthFloat32,
                            .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, -1, 0, 0, a * 0.1f, 0},
                            .origin = {0, 0, 6},
                            .rotation = {0, 0, 0, 1}};
}

static void TestDepth(void)
{
    // A box and a sphere of diameter 0.6 falling down z onto the image's
    // floor rest on it; a box out of the image's view is killed.
    for (int32_t outside = 0; outside < 3; ++outside)
    {
        mvfxEmitterDef emitter = outside == 2 ? Rigid(mvfx_rigidSphere, 0.6f, 0.6f, 0.6f)
                                              : Rigid(mvfx_rigidBox, 1, 1, 1);
        emitter.motion.gravity[1] = 0.0f;
        emitter.motion.gravity[2] = -9.81f;
        emitter.colliders = NULL;
        emitter.colliderCount = 0;
        emitter.collision.depthImage = true;
        emitter.collision.depthThickness = 0.5f;
        emitter.collision.depthSamples = 4;
        emitter.collision.killOutsideView = true;
        double place[3] = {outside == 1 ? 20.0 : 0.5, 0.3, 1.5};
        static const float still[3] = {0, 0, 0};
        Open(&emitter, place, still);
        const mvfxDepthImage image = Floor();
        CHECK(mvfxInstance_SetDepthImage(s_scene.context, s_scene.instance, &image) == mvfx_success,
              "depth image");
        Steps(90);
        if (outside == 1)
        {
            CHECK(Stats().live == 0, "killed outside the view");
        }
        else
        {
            float rest = outside == 2 ? 0.3f : 0.5f;
            CHECK(fabsf(X(2) - rest) <= 0.01f && Speed() <= 0.01f, "resting on the image");
        }
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestTippingAngle(void)
{
    // A box 0.4 wide and 0.6 tall, held from sliding, tips where its
    // center passes over its lower edge, at atan(0.4 / 0.6), 33.7 degrees:
    // it stands at 32.5 and tips at 36; parked at 33.7 it stands steady,
    // its spin never turning back and below 0.01 a radian a second.
    static const float still[3] = {0, 0, 0};
    static const float degrees[3] = {32.5f, 33.69f, 36.0f};
    bool right = true;
    for (int32_t d = 0; d < 3; ++d)
    {
        float angle = degrees[d] * 0.017453293f;
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 0.4f, 0.6f, 0.4f);
        Tilt(&emitter, angle);
        s_colliders[0].normal[0] = -sinf(angle);
        s_colliders[0].normal[1] = cosf(angle);
        emitter.rigid.staticFriction = 1.5f;
        emitter.rigid.dynamicFriction = 1.4f;
        double place[3] = {-0.3 * sin((double)angle), 0.3 * cos((double)angle), 0};
        Open(&emitter, place, still);
        float lowest = 1.0f;
        float spin = 0.0f;
        float last = 0.0f;
        int32_t turns = 0;
        for (int32_t i = 0; i < 180; ++i)
        {
            Steps(1);
            float axis[3];
            Axis(1, axis);
            lowest = fminf(lowest,
                           axis[0] * s_colliders[0].normal[0] + axis[1] * s_colliders[0].normal[1]);
            float w = W(2);
            turns += fabsf(w) > 1e-3f && last * w < 0.0f ? 1 : 0;
            last = fabsf(w) > 1e-3f ? w : last;
            spin = i >= 60 ? fmaxf(spin, fabsf(w)) : spin;
        }
        right = right && (d == 2 ? lowest < 0.5f : lowest > 0.9999f && spin <= 0.01f && turns == 0);
        mvfxDestroyContext(s_scene.context);
    }
    // Held at 30 degrees it does not creep (its reach measured from the
    // patch's center, which moves to the edge as friction loads it, it
    // crept 17 mm in 3 s); within a tenth of the limit the passes' noise
    // lets it creep a few millimeters a second until it sleeps, by 5 mm.
    for (int32_t sleeps = 0; sleeps < 2; ++sleeps)
    {
        float angle = (sleeps != 0 ? 33.6f : 30.0f) * 0.017453293f;
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 0.4f, 0.6f, 0.4f);
        Tilt(&emitter, angle);
        emitter.rigid.sleepSpeed = sleeps != 0 ? 0.05f : 0.0f;
        emitter.rigid.sleepTime = 0.5f;
        s_colliders[0].normal[0] = -sinf(angle);
        s_colliders[0].normal[1] = cosf(angle);
        emitter.rigid.staticFriction = 1.5f;
        emitter.rigid.dynamicFriction = 1.4f;
        double place[3] = {-0.3 * sin((double)angle), 0.3 * cos((double)angle), 0};
        Open(&emitter, place, still);
        Steps(180);
        float dx = X(0) - (float)place[0];
        float dy = X(1) - (float)place[1];
        right = right && sqrtf(dx * dx + dy * dy) <= (sleeps != 0 ? 0.005f : 0.0001f);
        mvfxDestroyContext(s_scene.context);
    }
    // Held at 0.95 of its limit (tan at 0.95 of the edge's), sleeping off,
    // it creeps, as the passes' friction and loads are noisy so near it:
    // pinned at today's 22 mm in 3 s, and 36 mm turned 45 degrees on its
    // upright, so the creep cannot grow unseen.
    for (int32_t turned = 0; turned < 2; ++turned)
    {
        float angle = atanf(0.95f * (turned != 0 ? 0.28284271f : 0.2f) / 0.3f);
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 0.4f, 0.6f, 0.4f);
        Tilt(&emitter, angle);
        emitter.orientation.min[1] = emitter.orientation.max[1] = turned != 0 ? 0.78539816f : 0.0f;
        emitter.rigid.sleepSpeed = 0.0f;
        s_colliders[0].normal[0] = -sinf(angle);
        s_colliders[0].normal[1] = cosf(angle);
        emitter.rigid.staticFriction = 1.5f;
        emitter.rigid.dynamicFriction = 1.4f;
        double place[3] = {-0.3 * sin((double)angle), 0.3 * cos((double)angle), 0};
        Open(&emitter, place, still);
        Steps(180);
        float dx = X(0) - (float)place[0];
        float dy = X(1) - (float)place[1];
        right = right && sqrtf(dx * dx + dy * dy) <= (turned != 0 ? 0.04f : 0.025f);
        mvfxDestroyContext(s_scene.context);
    }
    // Turned 45 degrees about its upright, it tips over a corner instead,
    // the diagonal's reach: at atan(0.283 / 0.3), 43.3 degrees; it stands
    // at 40 and tips at 46.
    static const float turned[2] = {40.0f, 46.0f};
    for (int32_t d = 0; d < 2; ++d)
    {
        float angle = turned[d] * 0.017453293f;
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 0.4f, 0.6f, 0.4f);
        Tilt(&emitter, angle);
        emitter.orientation.min[1] = emitter.orientation.max[1] = 0.78539816f;
        s_colliders[0].normal[0] = -sinf(angle);
        s_colliders[0].normal[1] = cosf(angle);
        emitter.rigid.staticFriction = 1.5f;
        emitter.rigid.dynamicFriction = 1.4f;
        double place[3] = {-0.3 * sin((double)angle), 0.3 * cos((double)angle), 0};
        Open(&emitter, place, still);
        Steps(180);
        float axis[3];
        Axis(1, axis);
        float up = axis[0] * s_colliders[0].normal[0] + axis[1] * s_colliders[0].normal[1];
        right = right && (d == 1 ? up < 0.5f : up > 0.999f);
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(right, "a box tips at its edge's angle");
}

static void TestTwist(void)
{
    // A unit box spun at 5 about y on a plane, dynamic friction 0.4: its
    // corners' mean reach 0.71 and inertia a sixth slow it at 16.6, so it
    // spins at 2.2 after a sixth of a second and stops by 0.3, in place.
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1, 1, 1);
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 5, 0}, .max = {0, 5, 0}};
    static const double place[3] = {0, 0.5, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(10);
    CHECK(fabsf(W(1) - 2.22f) <= 0.15f, "slowing by its corners' reach");
    Steps(50);
    CHECK(fabsf(W(1)) <= 1e-3f && fabsf(X(0)) <= 1e-3f && fabsf(X(2)) <= 1e-3f,
          "stopped spinning in place");
    mvfxDestroyContext(s_scene.context);
}

static void TestTipping(void)
{
    // On a slope of 0.35 (tan 0.37, held from sliding by 0.6), a box 0.4
    // wide and 0.6 tall stands (it would tip past tan 0.67); one 2 tall
    // tips over (past tan 0.2).
    float angle = 0.35f;
    static const float still[3] = {0, 0, 0};
    for (int32_t tall = 0; tall < 2; ++tall)
    {
        float height = tall == 1 ? 2.0f : 0.6f;
        mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 0.4f, height, 0.4f);
        Tilt(&emitter, angle);
        s_colliders[0].normal[0] = -sinf(angle);
        s_colliders[0].normal[1] = cosf(angle);
        double place[3] = {-0.5 * (double)height * sin((double)angle),
                           0.5 * (double)height * cos((double)angle), 0};
        Open(&emitter, place, still);
        Steps(120);
        float axis[3];
        Axis(1, axis);
        float up = axis[0] * s_colliders[0].normal[0] + axis[1] * s_colliders[0].normal[1];
        CHECK(tall == 1 ? fabsf(up) < 0.5f : up > 0.9999f,
              tall == 1 ? "a tall box tipped over" : "a short box stands");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = nullptr;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    int32_t refused = 0;
    for (int32_t c = 0; c < 9; ++c)
    {
        mvfxEmitterDef bad = Rigid(mvfx_rigidBox, 1, 1, 1);
        switch (c)
        {
        case 0:
            bad.rigid.shape = 4;
            break;
        case 1:
            bad.rigid.density = 0.0f;
            break;
        case 2:
            bad.rigid.substeps = 0;
            break;
        case 3:
            bad.rigid.restitution = 1.5f;
            break;
        case 4:
            bad.rigid.staticFriction = 0.1f;
            break;
        case 5:
            bad.rigid.bodyContacts = MVFX_MAX_BODY_CONTACTS + 1;
            break;
        case 6:
            bad.rigid.bodyContacts = -1;
            break;
        case 7:
            bad.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                     .points = 2,
                                     .spacing = 1,
                                     .direction = {1, 0, 0},
                                     .substeps = 1};
            break;
        default:
            bad.beam.enabled = true;
            break;
        }
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = &bad;
        def.emitterCount = 1;
        mvfxEffectId effect = {0};
        refused += mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid ? 1 : 0;
    }
    CHECK(refused == 9, "the checks");
    mvfxEmitterDef good = Rigid(mvfx_rigidBox, 1, 1, 1);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &good;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "a valid one");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestRest();
    TestBounce();
    TestRoll();
    TestTopple();
    TestSlope();
    TestSpheres();
    TestCapsule();
    TestStrikes();
    TestCapped();
    TestDeepest();
    TestInsideBox();
    TestStrikeEvents();
    TestHeldStrikes();
    TestTable();
    TestEdges();
    TestBeam();
    TestInsideBoxes();
    TestGrids();
    TestSceneQueries();
    TestDepth();
    TestGyroscopic();
    TestTwist();
    TestTipping();
    TestTippingAngle();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
