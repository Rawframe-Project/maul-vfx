// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles meeting each other: equal spheres trading their speeds
// at restitution 1, and moving on together at 0; spheres of eight times
// the mass bouncing as momentum says; bodies passing through each other
// without body contacts; three boxes stacked and still with their creep
// bounded; one large body among a thousand small ones; a box landing aside slowed by every impulse
// of its impacts; a heavy box landing on a light one without rebound; mixed spheres' overlapping
// pairs all found, and the same bits whatever the grid's layout; a narrow box on a wide one
// standing and tipping by the statics; a capsule lying across another; a box dropped on a box, and
// a sphere on a sphere rolling off; a box sliding to a stop on a wider one by friction, the one
// below held; a pile of spheres in a box settled without deep overlaps; the pool capped and
// counted; strikes counted on both bodies.

#include "particles.h"
#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene s_scene;

static const mvfxCollider s_ground = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};

enum
{
    MOST = 64
};

// The bodies' collisions by serial, as the host modules see them after
// motion in the last step.
static uint32_t s_collisions[MOST];

static void Count(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    for (int32_t i = 0; p->collisions != NULL && i < p->count; ++i)
    {
        if (p->serial[i] < MOST)
        {
            s_collisions[p->serial[i]] = p->collisions[i];
        }
    }
}

// An orientation the host gives body s_turned (by serial) before the
// next step's motion, once.
static uint32_t s_turned = UINT32_MAX;
static float s_turn[4];

static void Turn(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* p = &call->particles;
    for (int32_t i = 0; i < p->count; ++i)
    {
        if (p->serial[i] == s_turned)
        {
            for (int32_t k = 0; k < 4; ++k)
            {
                p->orientation[k][i] = s_turn[k];
            }
            s_turned = UINT32_MAX;
            return;
        }
    }
}

static const mvfxHostModule s_modules[2] = {
    {.stage = mvfx_stageBeforeMotion, .function = Turn},
    {.stage = mvfx_stageAfterMotion, .function = Count},
};

// Rigid bodies of a shape meeting each other and, with gravity, a plane
// at y = 0; eight substeps, no friction.
static mvfxEmitterDef Bodies(mvfxRigidShape shape, bool gravity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxMakeRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[0] = 0.0f;
    emitter.motion.gravity[1] = gravity ? -9.81f : 0.0f;
    emitter.motion.gravity[2] = 0.0f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1}, .max = {1, 1, 1}};
    emitter.rigid = (mvfxRigidDef){
        .enable = true, .shape = shape, .density = 1.0f, .substeps = 8, .bodyContacts = 8};
    emitter.collision.enable = true;
    emitter.colliders = gravity ? &s_ground : NULL;
    emitter.colliderCount = gravity ? 1 : 0;
    return emitter;
}

// Opens a scene of count bodies at places, with velocities and sizes
// (NULL for still and 1), and the host modules when hosted.
static void OpenHosted(const mvfxEmitterDef* emitter, const double* places, const float* velocities,
                       const float* sizes, int32_t count, bool hosted)
{
    memset(&s_scene, 0, sizeof s_scene);
    memset(s_collisions, 0, sizeof s_collisions);
    s_turned = UINT32_MAX;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEmitterDef withModules = *emitter;
    withModules.hostModules = hosted ? s_modules : NULL;
    withModules.hostModuleCount = hosted ? 2 : 0;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &withModules;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect, NULL) == mvfx_success, "effect");
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

static void Open(const mvfxEmitterDef* emitter, const double* places, const float* velocities,
                 const float* sizes, int32_t count)
{
    OpenHosted(emitter, places, velocities, sizes, count, true);
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

static TestParticle s_particles[MOST];

// The body of a serial (its order of birth), as the output writes it.
static const TestParticle* Body(uint32_t serial)
{
    int32_t count = ReadParticles(s_scene.context, s_scene.instance, 0, s_particles, MOST);
    for (int32_t i = 0; i < count && i < MOST; ++i)
    {
        if (s_particles[i].serial == serial)
        {
            return &s_particles[i];
        }
    }
    CHECK(false, "the body read");
    return &s_particles[0];
}

// Body serial's position or velocity along axis k.
static float X(uint32_t serial, int32_t k)
{
    return Body(serial)->position[k];
}

static float V(uint32_t serial, int32_t k)
{
    return Body(serial)->velocity[k];
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
    // sleeping off: the solver's own creep (3.4 mm/s at the top, 11 mm
    // aside in 3 s when measured, in a straight line) bounded, so that
    // sleeping never hides a regression in it; aside, by what the speed's
    // bound allows in 3 s.
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
        still = still && fabsf(X(i, 1) - (0.5f + (float)i)) <= 0.001f && aside <= 0.012f &&
                Speed(i) <= 0.004f;
    }
    CHECK(still, "a stack of three");
    mvfxDestroyContext(s_scene.context);
}

static void TestOneLarge(void)
{
    // A thousand small spheres (radius 0.05) 0.2 apart and one of radius
    // 3 beside them reaching into their first layer, no gravity, one
    // substep a step: the broadphase pairs the large one with the small
    // ones it overlaps, and all step on together.
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
    places[3 * SMALL] = -2.9;
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
    enum
    {
        MOST_PAIRS = 256
    };
    static int32_t a[MOST_PAIRS];
    static int32_t b[MOST_PAIRS];
    int32_t pairs = mvfxTestRigidPairs(s_scene.context, s_scene.instance, 0, a, b, MOST_PAIRS);
    bool large = false;
    for (int32_t c = 0; c < pairs && c < MOST_PAIRS; ++c)
    {
        large = large || a[c] == SMALL || b[c] == SMALL;
    }
    // How many the broadphase examines is no result; a bench is to
    // measure it (#23).
    CHECK(Stats().live == ALL && pairs > 0 && large, "one large body among a thousand");
    mvfxDestroyContext(s_scene.context);
}

// Sixty spheres of radii 0.05 to 0.8 (a hash's) about a box 3 wide,
// still, no gravity.
enum
{
    MIXED = 60
};

static void Mixed(double places[3 * MIXED], float sizes[3 * MIXED])
{
    for (uint32_t i = 0; i < MIXED; ++i)
    {
        uint32_t h = (i + 1u) * 2654435761u;
        for (int32_t k = 0; k < 3; ++k)
        {
            h ^= h >> 13;
            h *= 2246822519u;
            places[3 * i + (uint32_t)k] = 3.0 * (double)(h & 0xFFFFu) / 65536.0 - 1.5;
        }
        h ^= h >> 15;
        float radius = 0.05f + 0.75f * (float)(h & 0xFFFFu) / 65536.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            sizes[3 * i + (uint32_t)k] = 2.0f * radius;
        }
    }
}

static void TestPairsFound(void)
{
    // Mixed spheres overlapping where they lie: every pair overlapping is
    // in the pool, whatever the levels of the two (by brute force over
    // all pairs).
    static double places[3 * MIXED];
    static float sizes[3 * MIXED];
    static float still[3 * MIXED];
    Mixed(places, sizes);
    mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, false);
    emitter.capacity = MIXED;
    emitter.hostSpawnCapacity = MIXED;
    emitter.rigid.substeps = 1;
    emitter.rigid.bodyContacts = 64;
    Open(&emitter, places, still, sizes, MIXED);
    Steps(1);
    static float x[3 * MIXED];
    static float size[MIXED];
    CHECK(ReadParticles(s_scene.context, s_scene.instance, 0, s_particles, MOST) == MIXED,
          "all read");
    for (int32_t i = 0; i < MIXED; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            x[3 * i + k] = s_particles[i].position[k];
        }
        size[i] = s_particles[i].size[0];
    }
    Steps(1);
    enum
    {
        PAIRS = MIXED * (MIXED - 1) / 2
    };
    static int32_t pairA[PAIRS];
    static int32_t pairB[PAIRS];
    int32_t pairs = mvfxTestRigidPairs(s_scene.context, s_scene.instance, 0, pairA, pairB, PAIRS);
    bool found = pairs > 0 && pairs <= PAIRS;
    int32_t overlapping = 0;
    for (int32_t i = 0; i < MIXED; ++i)
    {
        for (int32_t j = i + 1; j < MIXED; ++j)
        {
            float d = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float g = x[3 * i + k] - x[3 * j + k];
                d += g * g;
            }
            float reach = 0.5f * (size[i] + size[j]);
            if (!(sqrtf(d) < 0.98f * reach))
            {
                continue;
            }
            overlapping += 1;
            bool met = false;
            for (int32_t c = 0; c < pairs && c < PAIRS && !met; ++c)
            {
                met = pairA[c] == i && pairB[c] == j;
            }
            found = found && met;
        }
    }
    CHECK(found && overlapping >= 20, "every overlapping pair found");
    mvfxDestroyContext(s_scene.context);
}

static void TestLayoutFree(void)
{
    // The mixed spheres falling onto the ground, with room for 60 and for
    // 4000 (tables of other sizes, the grid's keys laid out otherwise):
    // the same bits a second on, pairs met in the bodies' order.
    static double places[3 * MIXED];
    static float sizes[3 * MIXED];
    static float still[3 * MIXED];
    Mixed(places, sizes);
    static float after[2][3 * MIXED];
    for (int32_t room = 0; room < 2; ++room)
    {
        mvfxEmitterDef emitter = Bodies(mvfx_rigidSphere, true);
        emitter.capacity = room == 0 ? MIXED : 4000;
        emitter.hostSpawnCapacity = MIXED;
        emitter.rigid.bodyContacts = 16;
        for (int32_t i = 0; i < MIXED; ++i)
        {
            places[3 * i + 1] += 2.0;
        }
        Open(&emitter, places, still, sizes, MIXED);
        for (int32_t i = 0; i < MIXED; ++i)
        {
            places[3 * i + 1] -= 2.0;
        }
        Steps(60);
        for (int32_t i = 0; i < MIXED; ++i)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                after[room][3 * i + k] = X((uint32_t)i, k);
            }
        }
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(memcmp(after[0], after[1], sizeof after[0]) == 0, "the same whatever the grid's layout");
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

static void TestNarrowOnWide(void)
{
    // A box 0.4 wide and 0.6 tall on a wide box 2 by 1, sideways 0.6 up,
    // at, or 0.6 down the slope (gravity turned), friction 1.5: by the
    // statics it tips at atan(0.4 / 0.6), 33.7 degrees, on any support,
    // and the wide one, held by the ground, stays put. At 30 degrees it
    // stands, still within 3 mm of where it was set (it crept 13 mm in 3 s
    // before post-stabilization held the pair's contacts in place); at 38
    // it has tipped (between bodies the passes' noise lets it stand to
    // 37); the wide box moves at most 1 mm.
    static const float still[6] = {0};
    bool right = true;
    for (int32_t tips = 0; tips < 2; ++tips)
    {
        for (int32_t at = -1; at <= 1; ++at)
        {
            float angle = (tips != 0 ? 38.0f : 30.0f) * 0.017453293f;
            mvfxEmitterDef emitter = Bodies(mvfx_rigidBox, true);
            emitter.rigid.staticFriction = 1.5f;
            emitter.rigid.dynamicFriction = 1.4f;
            emitter.rigid.sleepSpeed = 0.0f;
            emitter.motion.gravity[0] = 9.81f * sinf(angle);
            emitter.motion.gravity[1] = -9.81f * cosf(angle);
            double aside = 0.6 * (double)at;
            const double places[6] = {0, 0.5, 0, aside, 1.3005, 0};
            static const float sizes[6] = {2, 1, 2, 0.4f, 0.6f, 0.4f};
            Open(&emitter, places, still, sizes, 2);
            Steps(180);
            float z = Body(1)->orientation[2];
            float up = 1.0f - 2.0f * z * z;
            float moved = fabsf(X(1, 0) - (float)aside);
            float wide = fabsf(X(0, 0)) + fabsf(X(0, 1) - 0.5f);
            right =
                right && wide <= 0.001f && (tips != 0 ? up < 0.5f : up > 0.999f && moved <= 0.003f);
            mvfxDestroyContext(s_scene.context);
        }
    }
    CHECK(right, "a narrow box on a wide one tips at its own limit");
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
    s_turn[0] = 0.0f;
    s_turn[1] = 0.0f;
    s_turn[2] = 0.70710678f;
    s_turn[3] = 0.70710678f;
    s_turned = 0;
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
    CHECK(Stats().rigidContactsDropped > 0u, "the pool capped");
    mvfxDestroyContext(s_scene.context);
    static const double places[6] = {-1, 0, 0, 0.5, 0, 0};
    static const float velocities[6] = {2, 0, 0, 0, 0, 0};
    mvfxEmitterDef spheres = Bodies(mvfx_rigidSphere, false);
    spheres.rigid.restitution = 1.0f;
    Open(&spheres, places, velocities, NULL, 2);
    Steps(60);
    CHECK(s_collisions[0] == 1u && s_collisions[1] == 1u, "a strike on both");
    mvfxDestroyContext(s_scene.context);
}

// Two bodies' places, velocities and orientations after each step.
enum
{
    TRACK_STEPS = 60
};

typedef struct Track
{
    float x[2][3];
    float v[2][3];
    float q[2][4];
} Track;

static Track s_unit[TRACK_STEPS];
static Track s_scaled[TRACK_STEPS];

// A box 0.4 by 0.6 by 0.4 sliding at 0.5 on a 2 by 1 by 2 box on the
// ground, both spun about the vertical (the one below slowed by the
// ground, the one above twisting on it), their sizes, places, velocities
// and gravity times 2^size and their density 2^density; sleeping off, its
// speed in the particles' units.
static void Stacked(int32_t size, int32_t density, Track* out)
{
    double unit = ldexp(1.0, size);
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox, true);
    emitter.rigid.density = (float)ldexp(1.0, density);
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.rigid.restitution = 0.3f;
    emitter.motion.gravity[1] = -9.81f * (float)unit;
    emitter.rigid.sleepSpeed = 0.0f;
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 3, 0}, .max = {0, 3, 0}};
    const double places[6] = {0, 0.5 * unit, 0, 0.3 * unit, 1.3005 * unit, 0};
    const float velocities[6] = {0, 0, 0, 0.5f * (float)unit, 0, 0};
    const float sizes[6] = {2.0f * (float)unit, (float)unit,        2.0f * (float)unit,
                            0.4f * (float)unit, 0.6f * (float)unit, 0.4f * (float)unit};
    Open(&emitter, places, velocities, sizes, 2);
    for (int32_t i = 0; i < TRACK_STEPS; ++i)
    {
        Steps(1);
        for (uint32_t b = 0; b < 2; ++b)
        {
            const TestParticle* body = Body(b);
            memcpy(out[i].x[b], body->position, sizeof out[i].x[b]);
            memcpy(out[i].v[b], body->velocity, sizeof out[i].v[b]);
            memcpy(out[i].q[b], body->orientation, sizeof out[i].q[b]);
        }
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
        for (int32_t b = 0; b < 2; ++b)
        {
            for (int32_t k = 0; k < 4; ++k)
            {
                same = same && s_scaled[i].q[b][k] == s_unit[i].q[b][k];
            }
            for (int32_t k = 0; k < 3; ++k)
            {
                same = same &&
                       s_scaled[i].x[b][k] == (float)ldexp((double)s_unit[i].x[b][k], size) &&
                       s_scaled[i].v[b][k] == (float)ldexp((double)s_unit[i].v[b][k], size);
            }
        }
    }
    return same;
}

// Whether every body read is finite.
static bool AllFinite(int32_t count)
{
    bool finite = true;
    for (uint32_t b = 0; b < (uint32_t)count; ++b)
    {
        const TestParticle* body = Body(b);
        for (int32_t k = 0; k < 4; ++k)
        {
            finite = finite && isfinite(body->orientation[k]) &&
                     (k == 3 || (isfinite(body->position[k]) && isfinite(body->velocity[k])));
        }
    }
    return finite;
}

static void TestUnits(void)
{
    // A pair is solved in the lighter body's mass unit, each body in its
    // own length unit, and its turning in the smaller of their mass units
    // times their length units squared: powers of two that cancel. The
    // stack slides, twists and rests the same bits from sizes of 2^-40 to
    // 2^16 and densities of 2^-140 to 2^19, where a pair solved in the
    // particles' units had an inverse mass past binary32's range, and at
    // 2^-40 the square of a resting box's leftover speed (2^-67) fell below
    // binary32's normal range until sizes were taken in a length unit.
    static const int32_t cases[][2] = {{16, 19}, {16, -140}, {-40, 19}, {-40, -140}, {0, -140}};
    Stacked(0, 0, s_unit);
    bool same = true;
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; ++c)
    {
        Stacked(cases[c][0], cases[c][1], s_scaled);
        same = same && SameScaled(cases[c][0]);
    }
    CHECK(same, "a stack the same bits at any size and density");
    // A box 2^-40 across meeting one 8 across at 2^-40 a second, spun at 4
    // about their normal, no gravity: its mass is 2^-129 of the other's
    // and its inertia 2^-215. Positions round to 2^-24 of the large box's
    // reach, so where the small one is held is no result; that it stays
    // finite is. Without the host modules, whose check kills a body whose
    // angular velocity passes the spin cap, as the small one's does.
    static const float sizes[6] = {8, 8, 8, 0x1p-40f, 0x1p-40f, 0x1p-40f};
    static const double places[6] = {0, -4, 0, 0, 0x1p-41 * 0.99, 0};
    static const float velocities[6] = {0, 0, 0, 0, -0x1p-40f, 0};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox, false);
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 4, 0}, .max = {0, 4, 0}};
    OpenHosted(&emitter, places, velocities, sizes, 2, false);
    bool finite = true;
    for (int32_t i = 0; i < 30; ++i)
    {
        Steps(1);
        finite = finite && AllFinite(2);
    }
    CHECK(finite && Stats().live == 2, "a box 2^-43 of another's size meeting it stays finite");
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
    TestPairsFound();
    TestLayoutFree();
    TestNarrowOnWide();
    TestCapsules();
    TestDropped();
    TestSliding();
    TestPile();
    TestPoolAndStrikes();
    TestUnits();
    return s_failures == 0 ? 0 : 1;
}
