// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles: a sphere dropped on a plane coming to rest on it; a
// bounce at its restitution; a sliding sphere starting to roll at five
// sevenths of its speed (and sliding on without friction); a box dropped
// on an edge toppling onto a face; a box held on a slope by static
// friction and sliding down a steeper one; bodies kept inside and outside
// spheres; a capsule lying down; collisions counted only as strikes, and
// killing; contacts capped and counted; a box spun about its middle axis
// flipping by the gyroscopic term while its momentum and energy hold; the
// checks.

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

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = nullptr;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    int32_t refused = 0;
    for (int32_t c = 0; c < 9; ++c)
    {
        mvfxEmitterDef bad = Rigid(mvfx_rigidBox, 1, 1, 1);
        static const mvfxCollider grid = {
            .kind = mvfx_colliderBox, .halfExtents = {1, 1, 1}, .rotation = {0, 0, 0, 1}};
        switch (c)
        {
        case 0:
            bad.rigid.shape = 3;
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
            bad.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                     .points = 2,
                                     .spacing = 1,
                                     .direction = {1, 0, 0},
                                     .substeps = 1};
            break;
        case 6:
            bad.colliders = &grid;
            break;
        case 7:
            bad.collision.sceneQueries = true;
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
    TestGyroscopic();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
