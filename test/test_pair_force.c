// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Forces between particles: a pair pulled together and pushed apart with
// its momentum kept, a strength curve over the distance, a cloud against
// brute force in double, none beyond the radius or between coincident
// particles, steering's limit not holding it, the neighbor attributes
// still stored, the checks.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

#define MOST    64
#define STEP    (MVFX_TICKS_PER_SECOND / 8)
#define SECONDS 0.125

// The particles before motion: places, velocities after the pull,
// neighbor counts.
typedef struct Seen
{
    int32_t count;
    float place[MOST][3];
    float velocity[MOST][3];
    float neighbors[MOST];
} Seen;

static Seen s_seen;

static void Capture(const mvfxModuleCall* call, void* user)
{
    (void)user;
    const mvfxParticleArrays* a = &call->particles;
    s_seen.count = a->count;
    for (int32_t i = 0; i < a->count && i < MOST; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            s_seen.place[i][k] = a->position[k][i];
            s_seen.velocity[i][k] = a->velocity[k][i];
        }
        s_seen.neighbors[i] = a->neighborCount[i];
    }
}

static const mvfxHostModule s_capture = {.stage = mvfx_stageBeforeMotion, .function = Capture};

// Neighbors within 4; a pair force within 2 of a constant strength.
static mvfxEmitterDef Emitter(float strength)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.hostModules = &s_capture;
    emitter.hostModuleCount = 1;
    emitter.neighbors =
        (mvfxNeighborDef){.enable = true, .source = -1, .radius = 4.0f, .maxNeighbors = MOST};
    emitter.pairForce =
        (mvfxPairForceDef){.enable = true, .radius = 2.0f, .strength = {.value = strength}};
    return emitter;
}

static mvfxContext* s_context;

// Spawns, steps a tick (the births), then a step whose start the module
// sees.
static void Run(const mvfxEmitterDef* emitter, const double* places, const float* velocities,
                int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_context, instance, 0, &request) == mvfx_success, "spawned");
    memset(&s_seen, 0, sizeof s_seen);
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success &&
              mvfxContext_StepTicks(s_context, STEP) == mvfx_success && s_seen.count == count,
          "stepped");
}

static bool Near(float a, double b)
{
    return fabs((double)a - b) <= 1e-5 * (1.0 + fabs(b));
}

static void TestPair(void)
{
    // 1 apart at rest, a strength of 3: each toward the other at 3, the
    // momentum kept; -3 pushes them apart.
    static const double places[6] = {0, 0, 0, 1, 0, 0};
    static const float rest[6] = {0};
    for (int32_t sign = -1; sign <= 1; sign += 2)
    {
        mvfxEmitterDef emitter = Emitter(3.0f * (float)sign);
        Run(&emitter, places, rest, 2);
        CHECK(Near(s_seen.velocity[0][0], 3.0 * SECONDS * sign) &&
                  Near(s_seen.velocity[1][0], -3.0 * SECONDS * sign) &&
                  s_seen.velocity[0][0] + s_seen.velocity[1][0] == 0.0f &&
                  s_seen.neighbors[0] == 1.0f,
              "pulled or pushed, momentum kept, the attributes stored");
        mvfxDestroyContext(s_context);
    }
    // Beyond the pair radius, or exactly at it, within the neighbors':
    // nothing. At one place: no line, nothing.
    mvfxEmitterDef emitter = Emitter(3.0f);
    static const double far[6] = {0, 0, 0, 3, 0, 0};
    Run(&emitter, far, rest, 2);
    CHECK(s_seen.velocity[0][0] == 0.0f && s_seen.neighbors[0] == 1.0f, "beyond the radius");
    mvfxDestroyContext(s_context);
    static const double at[6] = {0, 0, 0, 2, 0, 0};
    Run(&emitter, at, rest, 2);
    CHECK(s_seen.velocity[0][0] == 0.0f && s_seen.neighbors[0] == 1.0f, "at the radius");
    mvfxDestroyContext(s_context);
    static const double same[6] = {1, 1, 1, 1, 1, 1};
    Run(&emitter, same, rest, 2);
    CHECK(s_seen.velocity[0][0] == 0.0f && s_seen.velocity[1][1] == 0.0f, "coincident");
    mvfxDestroyContext(s_context);
}

// A strength falling linearly from 1 at 0 to -1 at the radius.
static const mvfxCurveKey s_falling[2] = {
    {.time = 0.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear},
    {.time = 1.0f, .value = -1.0f, .interpolation = mvfx_interpolationLinear},
};

static double FallingAt(double t)
{
    return 2.0 * (1.0 - 2.0 * t);
}

static void TestCloud(void)
{
    // Thirty particles moving in a box of 3, the strength 2 times the
    // falling curve: each velocity's change against brute force in double.
    static double places[90];
    static float velocities[90];
    uint32_t random = 11u;
    for (int32_t i = 0; i < 90; ++i)
    {
        random = random * 1664525u + 1013904223u;
        places[i] = 3.0 * (double)(random >> 8u) / 16777216.0;
        random = random * 1664525u + 1013904223u;
        velocities[i] = (float)(random >> 8u) / 16777216.0f - 0.5f;
    }
    mvfxEmitterDef emitter = Emitter(2.0f);
    emitter.pairForce.strength.curve = (mvfxCurve){.keys = s_falling, .keyCount = 2};
    Run(&emitter, places, velocities, 30);
    bool ok = true;
    for (int32_t i = 0; i < 30; ++i)
    {
        double acc[3] = {0, 0, 0};
        for (int32_t j = 0; j < 30; ++j)
        {
            double d[3];
            double square = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                d[k] = (double)s_seen.place[j][k] - (double)s_seen.place[i][k];
                square += d[k] * d[k];
            }
            if (j == i || !(square < 4.0) || square == 0.0)
            {
                continue;
            }
            double distance = sqrt(square);
            double strength = FallingAt(distance / 2.0);
            for (int32_t k = 0; k < 3; ++k)
            {
                acc[k] += strength * d[k] / distance;
            }
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            double want = (double)velocities[3 * i + k] + acc[k] * SECONDS;
            ok = ok && fabs((double)s_seen.velocity[i][k] - want) <= 1e-3;
        }
    }
    CHECK(ok, "brute force over all pairs");
    mvfxDestroyContext(s_context);
}

static void TestNotHeld(void)
{
    // Seeking with a force limit of 0.5 and a pull of 3: the pull is not
    // held.
    mvfxEmitterDef emitter = Emitter(3.0f);
    emitter.steering = (mvfxSteeringDef){
        .enable = true,
        .viewAngle = 3.14159274f,
        .cruiseSpeed = 10.0f,
        .maxForce = 0.5f,
        .seek = {.weight = 100.0f, .point = {0, 50, 0}},
    };
    static const double places[6] = {0, 0, 0, 1, 0, 0};
    static const float rest[6] = {0};
    Run(&emitter, places, rest, 2);
    CHECK(Near(s_seen.velocity[0][0], 3.0 * SECONDS) && Near(s_seen.velocity[0][1], 0.5 * SECONDS),
          "the pull beside steering's limit");
    mvfxDestroyContext(s_context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    static const mvfxCurveKey backwards[2] = {{.time = 1.0f}, {.time = 0.0f}};
    mvfxEmitterDef emitter = Emitter(1.0f);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 6; ++rule)
    {
        emitter = Emitter(1.0f);
        mvfxPairForceDef* p = &emitter.pairForce;
        switch (rule)
        {
        case 0:
            emitter.neighbors.enable = false;
            break;
        case 1:
            p->radius = 0.0f;
            break;
        case 2:
            p->radius = 4.5f;
            break;
        case 3:
            p->strength.value = NAN;
            break;
        case 4:
            p->strength.value = -2.0f * MVFX_MAX_EXTENT;
            break;
        default:
            p->strength.curve = (mvfxCurve){.keys = backwards, .keyCount = 2};
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 4 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(s_context, &def, &effect) == want;
    }
    CHECK(refused == 6, "every check");
    emitter = Emitter(-MVFX_MAX_EXTENT);
    emitter.pairForce.radius = 4.0f;
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "the bounds");
    emitter.pairForce.enable = false;
    emitter.pairForce.radius = -1.0f;
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "not enabled, unread");
    mvfxEmitterDef beam = mvfxDefaultEmitterDef();
    beam.rate = 0.0f;
    beam.capacity = 8;
    beam.ribbon.ids = mvfx_ribbonOne;
    beam.beam = (mvfxBeamDef){.enable = true, .segments = 4, .end = {.point = {1, 0, 0}}};
    beam.neighbors = Emitter(1.0f).neighbors;
    def.emitters = &beam;
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "a beam");
    beam.pairForce = Emitter(1.0f).pairForce;
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_errorInvalid, "not on a beam");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestPair();
    TestCloud();
    TestNotHeld();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
