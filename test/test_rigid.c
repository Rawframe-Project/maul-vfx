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
// flipping by the gyroscopic term while its momentum and energy hold;
// boxes turned by their free flow, their momentum, energy and orbit
// against Euler's equations; a box spun on a plane stopped by its
// friction at the rate its contacts' spread gives; a tall box tipping
// over on a slope a short one stands on;
// a box tipping at its edge's angle, standing steady parked at it and
// still below it, its creep near it bounded, and turned on its upright
// tipping over a corner at the diagonal's;
// a box spun and dropped in its own units: the same bits, scaled, at
// sizes and densities near binary32's ends, and each shape finite at the
// ends of the limits; needles 1e-20 of their length across, their inverse
// inertia held, finite dropped and spun, and needles spun at the most
// still turning;
// a sphere's and a capsule's sizes on the axes they do not set held to X:
// refused in the effect and a spawn request, taken from X's draw, a
// parameter's and an inherited one's X, killed when a host module sets
// them apart;
// the checks.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene s_scene;

static mvfxCollider s_colliders[2];

// The first body as the host modules see it after motion in the last step.
typedef struct Peeked
{
    float place[3];
    float velocity[3];
    float orientation[4];
    float angular[3];
    uint32_t collisions;
} Peeked;

static Peeked s_peeked;

static void Peek(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    if (p->count == 0)
    {
        return;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        s_peeked.place[k] = p->position[k][0];
        s_peeked.velocity[k] = p->velocity[k][0];
        s_peeked.angular[k] = p->angularVelocity[k][0];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        s_peeked.orientation[k] = p->orientation[k][0];
    }
    s_peeked.collisions = p->collisions != NULL ? p->collisions[0] : 0u;
}

static const mvfxHostModule s_peek = {.stage = mvfx_stageAfterMotion, .function = Peek};

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
    memset(&s_peeked, 0, sizeof s_peeked);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEmitterDef peeked = *emitter;
    peeked.hostModules = &s_peek;
    peeked.hostModuleCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &peeked;
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

static float X(int32_t k)
{
    return s_peeked.place[k];
}

static float V(int32_t k)
{
    return s_peeked.velocity[k];
}

static float W(int32_t k)
{
    return s_peeked.angular[k];
}

static void Q(float out[4])
{
    for (int32_t k = 0; k < 4; ++k)
    {
        out[k] = s_peeked.orientation[k];
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
    uint32_t landed = s_peeked.collisions;
    Steps(60);
    CHECK(landed >= 1 && s_peeked.collisions == landed, "strikes counted, not resting");
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
    uint32_t before = s_peeked.collisions;
    Steps(1);
    const mvfxEvent* events = NULL;
    int32_t n = 0;
    CHECK(mvfxInstance_GetEvents(s_scene.context, s_scene.instance, 0, &events, &n) == mvfx_success,
          "events");
    bool onSurface =
        n == 1 && (fabs(events[0].place[1]) <= 1e-4 || fabs(events[0].place[1] - 1.2) <= 1e-4);
    CHECK(s_peeked.collisions >= before + 2u && onSurface, "strikes counted, one event a step");
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

// A free box's motion over a step: its angular velocity w in its frame
// and its momentum's place in the world, in binary64.
typedef struct Free
{
    double body[3];
    double momentum[3];
} Free;

static Free FreeOf(const double inertia[3])
{
    float q[4];
    Q(q);
    Free f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float axis[3];
        Axis(k, axis);
        f.body[k] = (double)axis[0] * (double)W(0) + (double)axis[1] * (double)W(1) +
                    (double)axis[2] * (double)W(2);
    }
    for (int32_t j = 0; j < 3; ++j)
    {
        f.momentum[j] = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            float axis[3];
            Axis(k, axis);
            f.momentum[j] += (double)axis[j] * inertia[k] * f.body[k];
        }
    }
    return f;
}

// Euler's equations of a free body's angular velocity in its frame.
static void EulerOf(const double inertia[3], const double w[3], double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        int32_t a = (k + 1) % 3;
        int32_t b = (k + 2) % 3;
        out[k] = (inertia[a] - inertia[b]) * w[a] * w[b] / inertia[k];
    }
}

// Steps Euler's equations over h by RK4.
static void EulerStep(const double inertia[3], double w[3], double h)
{
    double k[4][3];
    double at[3];
    EulerOf(inertia, w, k[0]);
    for (int32_t s = 1; s < 4; ++s)
    {
        double share = s == 3 ? h : 0.5 * h;
        for (int32_t j = 0; j < 3; ++j)
        {
            at[j] = w[j] + share * k[s - 1][j];
        }
        EulerOf(inertia, at, k[s]);
    }
    for (int32_t j = 0; j < 3; ++j)
    {
        w[j] += h / 6.0 * (k[0][j] + 2.0 * k[1][j] + 2.0 * k[2][j] + k[3][j]);
    }
}

// How far a box of sizes x, y, z spun at w strays over 10 s: its world
// momentum's change over its size (into drift[0]), its energy's (into
// drift[1]), and the angle between its momentum in its frame and Euler's
// equations' stepped by RK4 from its first step (into drift[2]).
static void FreeDrift(float x, float y, float z, const float w[3], double drift[3])
{
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, x, y, z);
    emitter.motion.gravity[1] = 0.0f;
    emitter.collision.enabled = false;
    emitter.colliders = NULL;
    emitter.colliderCount = 0;
    emitter.angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {w[0], w[1], w[2]}, .max = {w[0], w[1], w[2]}};
    static const double place[3] = {0, 0, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    Steps(1);
    double sx = (double)x * (double)x;
    double sy = (double)y * (double)y;
    double sz = (double)z * (double)z;
    const double inertia[3] = {sy + sz, sx + sz, sx + sy};
    Free first = FreeOf(inertia);
    double size = 0.0;
    double energy = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        size += first.momentum[k] * first.momentum[k];
        energy += inertia[k] * first.body[k] * first.body[k];
    }
    size = sqrt(size);
    double euler[3] = {first.body[0], first.body[1], first.body[2]};
    for (int32_t k = 0; k < 3; ++k)
    {
        drift[k] = 0.0;
    }
    for (int32_t i = 0; i < 600; ++i)
    {
        Steps(1);
        for (int32_t j = 0; j < 64; ++j)
        {
            EulerStep(inertia, euler, 1.0 / 60.0 / 64.0);
        }
        Free f = FreeOf(inertia);
        double apart = 0.0;
        double e = 0.0;
        double along = 0.0;
        double ours = 0.0;
        double theirs = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double d = f.momentum[k] - first.momentum[k];
            apart += d * d;
            e += inertia[k] * f.body[k] * f.body[k];
            along += inertia[k] * f.body[k] * inertia[k] * euler[k];
            ours += inertia[k] * f.body[k] * inertia[k] * f.body[k];
            theirs += inertia[k] * euler[k] * inertia[k] * euler[k];
        }
        drift[0] = fmax(drift[0], sqrt(apart) / size);
        drift[1] = fmax(drift[1], fabs(e / energy - 1.0));
        double cosine = along / sqrt(ours * theirs);
        drift[2] = fmax(drift[2], acos(fmin(1.0, fmax(-1.0, cosine))));
    }
    mvfxDestroyContext(s_scene.context);
}

static void TestFreeFlow(void)
{
    // A box 0.5 by 1 by 1.5 spun at 20 rad/s near each of its principal
    // axes (near its middle one it flips over and back) turns by its free
    // flow for 10 s: its momentum in the world kept within 1e-4 of its
    // size, its energy within 1e-4, and its momentum in its frame within
    // 0.01 rad of Euler's equations'. Explicit, the first two were off by
    // up to 0.018 and 0.19 and the third by 3 rad.
    double most[3] = {0.0, 0.0, 0.0};
    for (int32_t axis = 0; axis < 3; ++axis)
    {
        float d[3] = {0.05f, 0.05f, 0.05f};
        d[axis] = 1.0f;
        d[(axis + 1) % 3] = 0.1f;
        float n = 20.0f / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float w[3] = {d[0] * n, d[1] * n, d[2] * n};
        double drift[3];
        FreeDrift(0.5f, 1.0f, 1.5f, w, drift);
        for (int32_t k = 0; k < 3; ++k)
        {
            most[k] = fmax(most[k], drift[k]);
        }
    }
    CHECK(most[0] <= 1e-4 && most[1] <= 1e-4 && most[2] <= 0.01,
          "a box's momentum, energy and orbit kept");
    // Boxes spun at 500 to 800 rad/s about tilted axes, up to 1.7 radians
    // a substep: their energy within 5e-3, their residual turns taken in
    // pieces (in one, a box's was off by up to 150%).
    static const float fast[4][7] = {{1.142f, 0.234f, 0.992f, 500.47f, 0.0362f, 0.081f, 0.187f},
                                     {1.2f, 1.612f, 0.391f, 572.25f, -0.7023f, -0.2796f, -0.2895f},
                                     {1.207f, 1.898f, 1.712f, 748.15f, 0.3316f, 0.3865f, -0.3627f},
                                     {0.633f, 0.332f, 1.405f, 800.0f, 0.1381f, 0.4512f, -2.084f}};
    double energy = 0.0;
    for (int32_t i = 0; i < 4; ++i)
    {
        const float* f = fast[i];
        float n = f[3] / sqrtf(f[4] * f[4] + f[5] * f[5] + f[6] * f[6]);
        const float w[3] = {f[4] * n, f[5] * n, f[6] * n};
        double drift[3];
        FreeDrift(f[0], f[1], f[2], w, drift);
        energy = fmax(energy, drift[1]);
    }
    CHECK(energy <= 5e-3, "fast boxes' energy kept");
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
    emitter->hostModules = &s_peek;
    emitter->hostModuleCount = 1;
    memset(&s_peeked, 0, sizeof s_peeked);
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

// A body's state after each step of a run.
typedef struct Track
{
    float x[3];
    float v[3];
    float q[4];
    float w[3];
} Track;

enum
{
    TRACK_STEPS = 120,
};

static Track s_unit[TRACK_STEPS];
static Track s_scaled[TRACK_STEPS];

// A 1 by 2 by 3 box spun about its middle axis and a little about the
// others, alone or tilted and dropped on the plane, its sizes, place and
// gravity times 2^size and its density 2^density.
static void Scaled(bool drop, int32_t size, int32_t density, Track* out)
{
    float unit = (float)ldexp(1.0, size);
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, unit, 2.0f * unit, 3.0f * unit);
    emitter.rigid.density = (float)ldexp(1.0, density);
    emitter.rigid.restitution = 0.3f;
    emitter.motion.gravity[1] = drop ? -9.81f * unit : 0.0f;
    emitter.collision.enabled = drop;
    emitter.colliderCount = drop ? 1 : 0;
    emitter.angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.05f, 4, 0.3f}, .max = {0.05f, 4, 0.3f}};
    Tilt(&emitter, 0.3f);
    const double place[3] = {0, 3.0 * (double)unit, 0};
    static const float still[3] = {0, 0, 0};
    Open(&emitter, place, still);
    for (int32_t i = 0; i < TRACK_STEPS; ++i)
    {
        Steps(1);
        for (int32_t k = 0; k < 3; ++k)
        {
            out[i].x[k] = X(k);
            out[i].v[k] = V(k);
            out[i].w[k] = W(k);
        }
        Q(out[i].q);
    }
    mvfxDestroyContext(s_scene.context);
}

// Whether a scaled run is the unit run's bits, its places and velocities
// times 2^size.
static bool SameScaled(int32_t size)
{
    bool same = true;
    for (int32_t i = 0; i < TRACK_STEPS; ++i)
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            same = same && s_scaled[i].q[k] == s_unit[i].q[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            same = same && s_scaled[i].w[k] == s_unit[i].w[k] &&
                   s_scaled[i].x[k] == (float)ldexp((double)s_unit[i].x[k], size) &&
                   s_scaled[i].v[k] == (float)ldexp((double)s_unit[i].v[k], size);
        }
    }
    return same;
}

static void TestUnits(void)
{
    // A body's mass and inertia are in its own units, powers of two that
    // cancel: spun alone, the box turns the same bits from sizes of 2^-120
    // to 2^18 and densities of 2^-140 to 2^19, where its momentum's square
    // passed binary32's range or fell below it (a NaN, or its spin lost);
    // dropped, it lands, tips and rests the same bits from sizes of 2^-40
    // to 2^16 (below, a substep's fall in the particles' units is below
    // binary32's normal range).
    static const int32_t spun[][2] = {{18, 19}, {18, -140}, {0, -140}, {-120, 19}, {-120, -140}};
    static const int32_t dropped[][2] = {{16, 19}, {16, -140}, {-40, 19}, {-40, -140}};
    Scaled(false, 0, 0, s_unit);
    bool same = true;
    for (size_t c = 0; c < sizeof spun / sizeof spun[0]; ++c)
    {
        Scaled(false, spun[c][0], spun[c][1], s_scaled);
        same = same && SameScaled(spun[c][0]);
    }
    CHECK(same, "spun the same bits at any size and density");
    Scaled(true, 0, 0, s_unit);
    same = true;
    for (size_t c = 0; c < sizeof dropped / sizeof dropped[0]; ++c)
    {
        Scaled(true, dropped[c][0], dropped[c][1], s_scaled);
        same = same && SameScaled(dropped[c][0]);
    }
    CHECK(same, "dropped the same bits at any size and density");
    // At the ends of the limits, each shape spun at MVFX_MAX_SPIN about
    // each axis and dropped on the plane stays finite.
    bool finite = true;
    for (int32_t shape = 0; shape < 3; ++shape)
    {
        for (int32_t end = 0; end < 2; ++end)
        {
            float size = end == 0 ? 1e-30f : MVFX_MAX_EXTENT;
            mvfxEmitterDef emitter = Rigid((mvfxRigidShape)shape, size, size, size);
            emitter.rigid.density = end == 0 ? 1e-30f : MVFX_MAX_EXTENT;
            emitter.angularVelocity =
                (mvfxVectorSource){.kind = mvfx_sourceRange,
                                   .min = {MVFX_MAX_SPIN, MVFX_MAX_SPIN, MVFX_MAX_SPIN},
                                   .max = {MVFX_MAX_SPIN, MVFX_MAX_SPIN, MVFX_MAX_SPIN}};
            const double place[3] = {0, 2.0 * (double)size, 0};
            static const float still[3] = {0, 0, 0};
            Open(&emitter, place, still);
            for (int32_t i = 0; i < 60; ++i)
            {
                Steps(1);
                float q[4];
                Q(q);
                for (int32_t k = 0; k < 3; ++k)
                {
                    finite = finite && isfinite(X(k)) && isfinite(V(k)) && isfinite(W(k)) &&
                             isfinite(q[k]) && isfinite(q[3]);
                }
            }
            mvfxDestroyContext(s_scene.context);
        }
    }
    CHECK(finite, "finite at the ends of sizes and densities");
}

static void TestNeedles(void)
{
    // A needle's inverse inertia about its long axis goes as its length
    // over its diameter squared, held at 2^40 in its units: capsules and
    // boxes 1e-20 across and 1 long, and 1e-14 across and MVFX_MAX_EXTENT
    // long, at the densities' ends, dropped upright on the plane turning
    // slowly or spun about each axis, stay finite (unheld, the first went
    // non-finite as they landed on an end, its friction turning them about
    // their long axis). Read through the output: no host module's check
    // kills one.
    static const float ends[2][2] = {{1e-20f, 1.0f}, {1e-14f, MVFX_MAX_EXTENT}};
    bool finite = true;
    for (int32_t c = 0; c < 8; ++c)
    {
        mvfxRigidShape shape = c % 2 == 0 ? mvfx_rigidCapsule : mvfx_rigidBox;
        const float* end = ends[c / 2 % 2];
        bool spun = c >= 4;
        mvfxEmitterDef emitter = Rigid(shape, end[0], end[1], end[0]);
        emitter.rigid.density = c % 4 < 2 ? 1e-30f : MVFX_MAX_EXTENT;
        emitter.motion.gravity[1] = -fminf(9.81f * end[1], MVFX_MAX_EXTENT);
        emitter.rigid.restitution = 0.3f;
        const float turning = spun ? 4.0f : 0.1f;
        emitter.angularVelocity = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                                     .min = {3 * turning, 2 * turning, turning},
                                                     .max = {3 * turning, 2 * turning, turning}};
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = &emitter;
        def.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
              "instance");
        const double place[3] = {0, 2.0 * (double)end[1], 0};
        mvfxSpawnRequest request = {.count = 1, .positions = place};
        CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
              "spawned");
        for (int32_t i = 0; i < 120; ++i)
        {
            Steps(1);
            TestParticle body;
            int32_t count = ReadParticles(s_scene.context, s_scene.instance, 0, &body, 1);
            finite = finite && count == 1;
            for (int32_t k = 0; count == 1 && k < 4; ++k)
            {
                finite = finite && isfinite(body.orientation[k]) &&
                         (k == 3 || (isfinite(body.position[k]) && isfinite(body.velocity[k])));
            }
        }
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(finite, "needles finite at the ends of sizes and densities");
    // Spun at MVFX_MAX_SPIN about each axis, alone, capsules and boxes 1e-6
    // and 1e-20 of their length across keep turning: their orientations
    // change in each of the last 20 of 60 steps. Stepped explicitly, the
    // gyroscopic term's rounding carried their spin onto their long axis
    // until a first-order turn mapped each orientation onto itself.
    bool turning = true;
    for (int32_t c = 0; c < 4; ++c)
    {
        float across = c < 2 ? 1e-6f : 1e-20f;
        mvfxEmitterDef emitter =
            Rigid(c % 2 == 0 ? mvfx_rigidCapsule : mvfx_rigidBox, across, 1.0f, across);
        emitter.motion.gravity[1] = 0.0f;
        emitter.collision.enabled = false;
        emitter.colliderCount = 0;
        emitter.angularVelocity =
            (mvfxVectorSource){.kind = mvfx_sourceRange,
                               .min = {MVFX_MAX_SPIN, MVFX_MAX_SPIN, MVFX_MAX_SPIN},
                               .max = {MVFX_MAX_SPIN, MVFX_MAX_SPIN, MVFX_MAX_SPIN}};
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = &emitter;
        def.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
              "instance");
        static const double origin[3] = {0, 0, 0};
        mvfxSpawnRequest request = {.count = 1, .positions = origin};
        CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
              "spawned");
        float last[4] = {0};
        for (int32_t i = 0; i < 60; ++i)
        {
            Steps(1);
            TestParticle body;
            bool read = ReadParticles(s_scene.context, s_scene.instance, 0, &body, 1) == 1;
            turning =
                turning && read && (i < 40 || memcmp(last, body.orientation, sizeof last) != 0);
            memcpy(last, body.orientation, sizeof last);
        }
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(turning, "needles spun at the most keep turning");
}

// The sizes of the first particles as the host modules see them after
// motion, and how many there were; with s_untie, the first one's Y set
// apart from its X before motion, once.
enum
{
    SIZED = 4
};

static float s_sizes[SIZED][3];
static int32_t s_sized;
static bool s_untie;

static void PeekSizes(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    s_sized = p->count;
    for (int32_t i = 0; i < p->count && i < SIZED && p->size[0] != NULL; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            s_sizes[i][k] = p->size[k][i];
        }
    }
}

static void Untie(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    if (s_untie && p->count > 0)
    {
        p->size[1][0] = 2.0f * p->size[0][0];
        s_untie = false;
    }
}

static const mvfxHostModule s_sizing[2] = {
    {.stage = mvfx_stageBeforeMotion, .function = Untie},
    {.stage = mvfx_stageAfterMotion, .function = PeekSizes},
};

// Whether an effect of emitters is made.
static bool Made(mvfxContext* context, const mvfxEmitterDef* emitters, int32_t count,
                 const float* parameters)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = count;
    def.parameters = parameters;
    def.parameterCount = parameters != NULL ? 1 : 0;
    mvfxEffectId effect = {0};
    return mvfxCreateEffect(context, &def, &effect) == mvfx_success;
}

// Steps emitters (their last one's sizes peeked) from four of the last
// one's bodies spawned at the origin with sizes (NULL to draw them), or
// none, for a second; whether they were spawned.
static bool Sized(const mvfxEmitterDef* emitters, int32_t count, const float* parameters,
                  const float* sizes, bool spawn)
{
    memset(&s_scene, 0, sizeof s_scene);
    memset(s_sizes, 0, sizeof s_sizes);
    s_sized = 0;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEmitterDef hosted[2];
    memcpy(hosted, emitters, (size_t)count * sizeof hosted[0]);
    hosted[count - 1].hostModules = s_sizing;
    hosted[count - 1].hostModuleCount = 2;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = hosted;
    def.emitterCount = count;
    def.parameters = parameters;
    def.parameterCount = parameters != NULL ? 1 : 0;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    static const double places[3 * SIZED] = {0, 1, 0, 2, 1, 0, 4, 1, 0, 6, 1, 0};
    mvfxSpawnRequest request = {.count = SIZED, .positions = places, .sizes = sizes};
    bool spawned = !spawn || mvfxInstance_Spawn(s_scene.context, s_scene.instance, count - 1,
                                                &request) == mvfx_success;
    Steps(60);
    return spawned;
}

// Whether the peeked sizes keep Y (for a sphere) and Z at X, and at want
// where it is not 0.
static bool Tied(mvfxRigidShape shape, const float want[3])
{
    bool tied = s_sized > 0;
    for (int32_t i = 0; i < s_sized && i < SIZED; ++i)
    {
        tied = tied && s_sizes[i][2] == s_sizes[i][0] &&
               (shape != mvfx_rigidSphere || s_sizes[i][1] == s_sizes[i][0]);
        for (int32_t k = 0; want != NULL && k < 3; ++k)
        {
            tied = tied && s_sizes[i][k] == want[k];
        }
    }
    return tied;
}

static void TestTiedSizes(void)
{
    // A size axis a shape does not set is X: a sphere's Y and Z, a
    // capsule's Z. A size source's ends or size over life per axis
    // otherwise are refused; a capsule's own Y, a box's three and the same
    // curves per axis are not.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = nullptr;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const mvfxCurveKey grow[2] = {{0, 1, 0, 0, mvfx_interpolationLinear},
                                         {1, 2, 0, 0, mvfx_interpolationLinear}};
    static const mvfxCurveKey shrink[2] = {{0, 1, 0, 0, mvfx_interpolationLinear},
                                           {1, 0.5f, 0, 0, mvfx_interpolationLinear}};
    const mvfxLifeCurve grows = {.value = 1, .curve = {.keys = grow, .keyCount = 2}};
    const mvfxLifeCurve shrinks = {.value = 1, .curve = {.keys = shrink, .keyCount = 2}};
    int32_t refused = 0;
    for (int32_t c = 0; c < 4; ++c)
    {
        mvfxEmitterDef bad =
            Rigid(c == 2 ? mvfx_rigidCapsule : mvfx_rigidSphere, 1, c == 2 ? 3 : 1, 1);
        bad.size.min[1] = c == 0 ? 0.5f : bad.size.min[1];
        bad.size.max[2] = c == 1 || c == 2 ? 2.0f : bad.size.max[2];
        bad.overLife.size =
            (mvfxSizeOverLife){.enabled = c == 3, .perAxis = true, .size = {grows, shrinks, grows}};
        refused += Made(context, &bad, 1, NULL) ? 0 : 1;
    }
    mvfxEmitterDef good[3] = {Rigid(mvfx_rigidCapsule, 1, 3, 1), Rigid(mvfx_rigidBox, 1, 2, 3),
                              Rigid(mvfx_rigidSphere, 1, 1, 1)};
    good[2].overLife.size =
        (mvfxSizeOverLife){.enabled = true, .perAxis = true, .size = {grows, grows, grows}};
    bool made = true;
    for (int32_t c = 0; c < 3; ++c)
    {
        made = made && Made(context, &good[c], 1, NULL);
    }
    CHECK(refused == 4 && made, "sizes unlike X on axes a shape does not set refused");
    mvfxDestroyContext(context);
    // A spawn request's size is refused there too.
    mvfxEmitterDef sphere = Rigid(mvfx_rigidSphere, 1, 1, 1);
    static const float apart[3 * SIZED] = {1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1};
    static const float even[3 * SIZED] = {1, 1, 1, 0.5f, 0.5f, 0.5f, 1, 1, 1, 2, 2, 2};
    bool refusedSpawn = !Sized(&sphere, 1, NULL, apart, true);
    mvfxDestroyContext(s_scene.context);
    bool spawned = Sized(&sphere, 1, NULL, even, true);
    CHECK(refusedSpawn && spawned && Tied(mvfx_rigidSphere, NULL),
          "a spawn request's sizes unlike X refused");
    mvfxDestroyContext(s_scene.context);
    // A size drawn axis by axis takes X's draw for the axes the shape does
    // not set.
    for (int32_t shape = 0; shape < 2; ++shape)
    {
        mvfxEmitterDef drawn = Rigid((mvfxRigidShape)shape, 1, 3, 1);
        drawn.size =
            (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0.5f, 2, 0.5f}, .max = {1, 3, 1}};
        drawn.size.min[1] = shape == 0 ? 0.5f : 2.0f;
        drawn.size.max[1] = shape == 0 ? 1.0f : 3.0f;
        Sized(&drawn, 1, NULL, NULL, true);
        CHECK(Tied((mvfxRigidShape)shape, NULL) && s_sizes[0][0] != s_sizes[1][0],
              "drawn sizes take X's draw");
        mvfxDestroyContext(s_scene.context);
    }
    // A parameter's size keeps the axes a shape sets, its others ignored.
    static const float parameter[4] = {0.5f, 2, 3, 0};
    for (int32_t shape = 0; shape < 2; ++shape)
    {
        mvfxEmitterDef scaled = Rigid((mvfxRigidShape)shape, 1, 1, 1);
        scaled.size.kind = mvfx_sourceParameter;
        scaled.size.parameter = 0;
        const float want[3] = {0.5f, shape == 0 ? 0.5f : 2.0f, 0.5f};
        Sized(&scaled, 1, parameter, NULL, true);
        CHECK(Tied((mvfxRigidShape)shape, want), "a parameter's size tied to X");
        mvfxDestroyContext(s_scene.context);
    }
    // An inherited size, of a source of sprites 1 by 2 by 3 ending at 0.1
    // seconds, likewise.
    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), Rigid(mvfx_rigidCapsule, 1, 1, 1)};
    emitters[0].rate = 40.0f;
    emitters[0].lifetime = mvfxRangeSource(0.1f, 0.1f);
    emitters[0].size =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 2, 3}, .max = {1, 2, 3}};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 64;
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 1,
                                              .inheritSize = true,
                                              .capacity = 8};
    static const float inherited[3] = {1, 2, 1};
    Sized(emitters, 2, NULL, NULL, false);
    CHECK(Tied(mvfx_rigidCapsule, inherited), "an inherited size tied to X");
    mvfxDestroyContext(s_scene.context);
    // A host module that sets Y apart from X kills the body.
    s_untie = true;
    Sized(&sphere, 1, NULL, even, true);
    CHECK(Stats().valuesKilled == 1 && Stats().live == SIZED - 1,
          "a host module's size unlike X kills its body");
    mvfxDestroyContext(s_scene.context);
}

// The angular velocity a host module writes on each body about an axis
// (X unless set) before motion, once there are bodies.
enum
{
    SPUN = 7
};

static float s_spinWritten[SPUN];
static int32_t s_spinAxes[SPUN];
static bool s_spinning;

static void Spin(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    for (int32_t i = 0; s_spinning && i < p->count && i < SPUN; ++i)
    {
        p->angularVelocity[s_spinAxes[i]][i] = s_spinWritten[i];
    }
    s_spinning = s_spinning && p->count == 0;
}

static void TestSpinBound(void)
{
    // A host module's angular velocity on a rigid body is checked against
    // what the body's own step may give it, not each axis against
    // MVFX_MAX_SPIN: its angular momentum within the cap's about every
    // axis, or its extent turned within 4 times the speed bound
    // (MVFX_MAX_SPEED), and either within 2^60 (1.15e18), past which the
    // step's binary32 squares overflow. Free spheres 1e-5 across spun at
    // 3.9e14 (kept, 3.9e9 at their surface) and 4.1e14 (killed), 1e4
    // across, their surface past the bound, at 5.1e6 (kept, within
    // 3 sqrt(3) times the cap: the cap on all three axes, times the trace
    // over a moment) and 5.3e6 (killed), one at NaN (killed), and 1e-20
    // across, their surface within the bound, at 1.1e18 (kept) and 1.2e18
    // (killed).
    static const mvfxHostModule spin = {.stage = mvfx_stageBeforeMotion, .function = Spin};
    static const float sizes[3 * SPUN] = {1e-5f, 1e-5f,  1e-5f,  1e-5f,  1e-5f,  1e-5f,  1e4f,
                                          1e4f,  1e4f,   1e4f,   1e4f,   1e4f,   1,      1,
                                          1,     1e-20f, 1e-20f, 1e-20f, 1e-20f, 1e-20f, 1e-20f};
    static const float written[SPUN] = {3.9e14f, 4.1e14f, 5.1e6f, 5.3e6f, NAN, 1.1e18f, 1.2e18f};
    memcpy(s_spinWritten, written, sizeof s_spinWritten);
    memset(s_spinAxes, 0, sizeof s_spinAxes);
    s_spinning = true;
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1, 1, 1);
    emitter.motion.gravity[1] = 0.0f;
    emitter.collision.enabled = false;
    emitter.colliderCount = 0;
    emitter.hostModules = &spin;
    emitter.hostModuleCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    static const double places[3 * SPUN] = {0,   1, 0, 2, 1,  0, 0, 1,  3e4, 0, 1,
                                            6e4, 8, 1, 0, 10, 1, 0, 12, 1,   0};
    mvfxSpawnRequest request = {.count = SPUN, .positions = places, .sizes = sizes};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(2);
    CHECK(Stats().valuesKilled == 4 && Stats().live == 3,
          "a body's spin held to what its own step gives it, not to the spin cap");
    // Those kept stay finite as their steps turn them.
    Steps(8);
    TestParticle kept[SPUN];
    int32_t count = ReadParticles(s_scene.context, s_scene.instance, 0, kept, SPUN);
    bool finite = count == 3 && Stats().valuesKilled == 4;
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            finite = finite && isfinite(kept[i].orientation[k]) &&
                     isfinite(kept[i].position[k % 3]) && isfinite(kept[i].velocity[k % 3]);
        }
    }
    CHECK(finite, "a body kept at its spin's bounds finite");
    mvfxDestroyContext(s_scene.context);
    // Its momentum is taken in its own frame: boxes 1e4 by 1e2 by 1e2 (a
    // least moment 1/5000 of the others), turned a quarter about Z so that
    // their long axis is Y's, two spun at 1e9 about Y (kept, about their
    // least moment) and one about X (killed, about a largest).
    static const float turnedSpins[3] = {1e9f, 1e9f, 1e9f};
    memcpy(s_spinWritten, turnedSpins, sizeof turnedSpins);
    s_spinAxes[0] = 1;
    s_spinAxes[1] = 0;
    s_spinAxes[2] = 1;
    s_spinning = true;
    memset(&s_scene, 0, sizeof s_scene);
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    emitter = Rigid(mvfx_rigidBox, 1e4f, 1e2f, 1e2f);
    emitter.motion.gravity[1] = 0.0f;
    emitter.collision.enabled = false;
    emitter.colliderCount = 0;
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0, 0, 1.5707964f}, .max = {0, 0, 1.5707964f}};
    emitter.hostModules = &spin;
    emitter.hostModuleCount = 1;
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    static const double apart[9] = {0, 0, 0, 0, 0, 1e5, 0, 0, 2e5};
    mvfxSpawnRequest three = {.count = 3, .positions = apart};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &three) == mvfx_success,
          "spawned");
    Steps(2);
    CHECK(Stats().valuesKilled == 1 && Stats().live == 2,
          "a turned body's momentum taken in its own frame");
    mvfxDestroyContext(s_scene.context);
    // A body of no volume has no inertia, and its step does not turn it:
    // its spin is held to the cap on each axis. Boxes 1 by 1 by 0 spun at
    // 9e5 about X (kept) and 2e6 (killed, though their surface is within
    // the speed bound).
    static const float flatSpins[2] = {9e5f, 2e6f};
    memcpy(s_spinWritten, flatSpins, sizeof flatSpins);
    memset(s_spinAxes, 0, sizeof s_spinAxes);
    s_spinning = true;
    memset(&s_scene, 0, sizeof s_scene);
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    emitter = Rigid(mvfx_rigidBox, 1, 1, 0);
    emitter.motion.gravity[1] = 0.0f;
    emitter.collision.enabled = false;
    emitter.colliderCount = 0;
    emitter.hostModules = &spin;
    emitter.hostModuleCount = 1;
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxSpawnRequest flat = {.count = 2, .positions = apart};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &flat) == mvfx_success,
          "spawned");
    Steps(2);
    CHECK(Stats().valuesKilled == 1 && Stats().live == 1,
          "a body of no volume held to the spin cap on each axis");
    mvfxDestroyContext(s_scene.context);
}

// Writes an angular velocity of 1e11 about Z on every body before motion.
static void Whirl(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    for (int32_t i = 0; i < p->count; ++i)
    {
        p->angularVelocity[0][i] = 0.0f;
        p->angularVelocity[1][i] = 0.0f;
        p->angularVelocity[2][i] = 1e11f;
    }
}

static void TestFastTurn(void)
{
    // A body turning past 2^24 radians a half substep turns by its angle's
    // exact value, where binary32 holds no fraction of a turn: a sphere
    // 1e-3 across spun at 1e11 (about 1e8 radians a half substep, within
    // the spin a host module may give it) has a new orientation each step,
    // unit and about Z.
    static const mvfxHostModule modules[2] = {{.stage = mvfx_stageBeforeMotion, .function = Whirl},
                                              {.stage = mvfx_stageAfterMotion, .function = Peek}};
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Rigid(mvfx_rigidSphere, 1e-3f, 1e-3f, 1e-3f);
    emitter.motion.gravity[1] = 0.0f;
    emitter.collision.enabled = false;
    emitter.colliderCount = 0;
    emitter.hostModules = modules;
    emitter.hostModuleCount = 2;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    static const double place[3] = {0, 1, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    Steps(2);
    float before[4];
    Q(before);
    bool turned = true;
    for (int32_t s = 0; s < 4; ++s)
    {
        Steps(1);
        float q[4];
        Q(q);
        float length = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        turned = turned && fabsf(length - 1.0f) < 1e-5f && q[0] == 0.0f && q[1] == 0.0f &&
                 (q[2] != before[2] || q[3] != before[3]);
        memcpy(before, q, sizeof before);
    }
    CHECK(turned && Stats().live == 1, "a body turning past 2^24 radians a half substep turned");
    mvfxDestroyContext(s_scene.context);
}

// Whether a host module sinks the body far along x half into the floor
// before motion.
static bool s_sinking;

static void Sink(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    for (int32_t i = 0; s_sinking && i < p->count; ++i)
    {
        p->position[1][i] = p->position[0][i] > 5e6f ? 0.0f : p->position[1][i];
    }
    s_sinking = false;
}

// Whether every body left by the last step moves within the speed bound
// along each axis.
static bool AreHeld(float fastest)
{
    TestParticle bodies[2];
    int32_t count = ReadParticles(s_scene.context, s_scene.instance, 0, bodies, 2);
    bool held = count == 2;
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            held = held && fabsf(bodies[i].velocity[k]) <= fastest;
        }
    }
    return held;
}

static void TestHeldSpeed(void)
{
    // A body's velocity is held within MVFX_MAX_SPEED along each axis
    // (times the instance's scale in world space) at its step's end, as
    // every particle's, so a host module's check kills none: boxes 1e6 by
    // 5e5 by 2.5e5 in an instance of scale 1e-6 (a bound of 1e3) pushed out
    // of the floor faster, one born in it, then one sunk half in by a host
    // module before a later step.
    static const mvfxHostModule modules[2] = {{.stage = mvfx_stageBeforeMotion, .function = Sink},
                                              {.stage = mvfx_stageAfterMotion, .function = Peek}};
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Rigid(mvfx_rigidBox, 1e6f, 5e5f, 2.5e5f);
    emitter.space = mvfx_spaceWorld;
    emitter.rigid.restitution = 0.3f;
    emitter.angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.3f, 0.2f, 0.1f}, .max = {0.3f, 0.2f, 0.1f}};
    emitter.hostModules = modules;
    emitter.hostModuleCount = 2;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.scale = 1e-6f;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    static const double places[6] = {0, 2, 0, 1e7, 2e6, 0};
    mvfxSpawnRequest request = {.count = 2, .positions = places};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    s_sinking = false;
    Steps(1);
    CHECK(AreHeld(1e3f) && Stats().valuesKilled == 0, "a body born in the floor held");
    s_sinking = true;
    Steps(1);
    CHECK(AreHeld(1e3f) && Stats().valuesKilled == 0, "a body sunk in the floor held");
    mvfxDestroyContext(s_scene.context);
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
    TestFreeFlow();
    TestTwist();
    TestTipping();
    TestTippingAngle();
    TestUnits();
    TestNeedles();
    TestTiedSizes();
    TestSpinBound();
    TestFastTurn();
    TestHeldSpeed();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
