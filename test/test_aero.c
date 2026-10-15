// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sheets' aerodynamics: a flat sheet thrown down slowing exactly as its
// mean velocity's v -= (k A h / N) v^2 a substep, and falling to near
// its terminal speed sqrt(N g / (k A)) by normal drag; an edge-on one slowed by tangent drag
// alone; a sheet at 45 degrees in a wind pushed along its normal, lift
// equal to drag; a torn sheet falling free; a drag too strong for a
// substep never reversing a point; the checks.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 64

typedef struct Record
{
    float position[3];
    float velocity[3];
} Record;

static const mvfxLayoutField s_fields[2] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
};

static const mvfxView s_view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Record records[MOST];
    int64_t count;
} Scene;

static Scene s_scene;

// A four by four sheet across x and down z, 0.5 apart, rigid, eight
// substeps, under gravity, with a normal drag.
static mvfxEmitterDef Sheet(float normalDrag, float tangentDrag)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 1;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                 .points = 4,
                                 .rows = 4,
                                 .spacing = 0.5f,
                                 .direction = {1, 0, 0},
                                 .downDirection = {0, 0, 1},
                                 .substeps = 8,
                                 .normalDrag = normalDrag,
                                 .tangentDrag = tangentDrag};
    return emitter;
}

static void Open(const mvfxEmitterDef* emitter, const float* velocity)
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
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = (uint32_t)sizeof(Record);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
    static const double root[3] = {0, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = root, .velocities = velocity};
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

// The points' mean velocity.
static void Mean(double out[3])
{
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                      NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success &&
              s_scene.count == 16,
          "read");
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = 0.0;
        for (int32_t i = 0; i < 16; ++i)
        {
            out[k] += (double)s_scene.records[i].velocity[k] / 16.0;
        }
    }
}

static void TestTerminal(void)
{
    // Thrown down at 5, no gravity, its first step after its birth's (a
    // newborn moves as particles) while it is flat: each substep takes
    // k A v^2 h of the 16 points' momentum (the constraints keep it), A the
    // 2.25 of its quads, within a thousandth.
    mvfxEmitterDef emitter = Sheet(1.0f, 0.0f);
    emitter.motion.gravity[1] = 0.0f;
    static const float thrown[3] = {0, -5, 0};
    Open(&emitter, thrown);
    Steps(1);
    double mean[3];
    Mean(mean);
    CHECK(fabs(mean[1] + 5.0) <= 1e-6, "its birth's step");
    Steps(1);
    Mean(mean);
    double v = 5.0;
    for (int32_t s = 0; s < 8; ++s)
    {
        v -= 2.25 * v * v / 480.0 / 16.0;
    }
    CHECK(fabs(-mean[1] - v) <= 1e-3 * v && v < 4.95, "a substep's drag");
    mvfxDestroyContext(s_scene.context);
    // Falling: 16 points, k = 1, the terminal speed sqrt(16 g / 2.25),
    // 8.352, within five percent (the sheet cups, its edges' points in
    // fewer triangles, and shows less area).
    emitter = Sheet(1.0f, 0.0f);
    Open(&emitter, NULL);
    Steps(480);
    Mean(mean);
    double terminal = sqrt(16.0 * 9.81 / 2.25);
    CHECK(fabs(-mean[1] - terminal) <= 0.05 * terminal, "the terminal speed");
    mvfxDestroyContext(s_scene.context);
    // Edge on (in the x-y plane), falling along itself: a normal drag of
    // 1000 does nothing, a tangent drag of 1 brings the same terminal
    // speed.
    emitter = Sheet(1000.0f, 1.0f);
    emitter.unit.downDirection[1] = -1.0f;
    emitter.unit.downDirection[2] = 0.0f;
    Open(&emitter, NULL);
    Steps(480);
    Mean(mean);
    CHECK(fabs(-mean[1] - terminal) <= 0.05 * terminal, "edge on, tangent drag alone");
    mvfxDestroyContext(s_scene.context);
}

static void TestLift(void)
{
    // At 45 degrees (normal (1, 1, 0) / sqrt 2) in a wind of 10 along x,
    // no gravity: pushed along its normal, as much up (lift) as along the
    // wind (drag). The wind's own pull is a millionth: it moves the air.
    mvfxEmitterDef emitter = Sheet(0.5f, 0.0f);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.direction[0] = 0.0f;
    emitter.unit.direction[2] = 1.0f;
    emitter.unit.downDirection[0] = 1.0f;
    emitter.unit.downDirection[1] = -1.0f;
    emitter.unit.downDirection[2] = 0.0f;
    static const mvfxForce wind = {.kind = mvfx_forceWind, .vector = {10, 0, 0}, .strength = 1e-6f};
    emitter.forces = &wind;
    emitter.forceCount = 1;
    Open(&emitter, NULL);
    Steps(20);
    double mean[3];
    Mean(mean);
    CHECK(mean[0] > 0.5 && fabs(mean[1] - mean[0]) <= 0.01 * mean[0] &&
              fabs(mean[2]) <= 0.01 * mean[0],
          "pushed along its normal");
    mvfxDestroyContext(s_scene.context);
}

static void TestTorn(void)
{
    // Every edge torn: no triangle, a free fall.
    mvfxEmitterDef emitter = Sheet(1.0f, 1.0f);
    emitter.unit.tearStrain = 50.0f;
    Open(&emitter, NULL);
    Steps(1);
    uint32_t* torn = mvfxAttributeArray(mvfxContextInstance(s_scene.context, s_scene.instance), 0,
                                        mvfx_attributeTorn);
    for (int32_t i = 0; i < 16; ++i)
    {
        torn[i] = 3u;
    }
    double before[3];
    Mean(before);
    Steps(60);
    double mean[3];
    Mean(mean);
    CHECK(fabs(mean[1] - before[1] + 9.81) <= 1e-3, "torn, falling free");
    mvfxDestroyContext(s_scene.context);
}

static void TestHeld(void)
{
    // Thrown down at 10 into a drag of 10^4: its mean velocity (the
    // constraints keep the momentum; they may turn points back as the sheet
    // folds) slowed, never turned back.
    mvfxEmitterDef emitter = Sheet(1e4f, 1e4f);
    emitter.motion.gravity[1] = 0.0f;
    static const float thrown[3] = {0, -10, 0};
    Open(&emitter, thrown);
    Steps(1);
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                      NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success,
          "read");
    Steps(1);
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                      NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success,
          "read");
    double mean[3];
    Mean(mean);
    CHECK(mean[1] <= 0.0 && mean[1] > -10.0, "never reversed");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Sheet(1.0f, 1.0f);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 6; ++rule)
    {
        emitter = Sheet(1.0f, 1.0f);
        float* drag = rule < 3 ? &emitter.unit.normalDrag : &emitter.unit.tangentDrag;
        *drag = rule % 3 == 0 ? -1.0f : rule % 3 == 1 ? NAN : 2.0f * MVFX_MAX_EXTENT;
        refused += mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
    }
    CHECK(refused == 6, "every check");
    emitter = Sheet(MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the bounds");
    emitter = Sheet(-1.0f, NAN);
    emitter.unit.kind = mvfx_unitChain;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "a chain's unread");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestTerminal();
    TestLift();
    TestTorn();
    TestHeld();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
