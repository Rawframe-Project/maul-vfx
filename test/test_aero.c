// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sheets' aerodynamics: a flat sheet thrown down slowing exactly as its
// mean velocity's v -= (k A h / N) v^2 a substep, and falling to near
// its terminal speed sqrt(N g / (k A)) by normal drag; an edge-on one slowed by tangent drag
// alone; a sheet at 45 degrees in a wind pushed along its normal, lift
// equal to drag; at the largest spacing and scale, and with an area past
// binary32's largest, a tangent drag alone keeping the speed along the
// normal; a triangle with an edge past 2^63 and one a spacing long taking
// its whole share; a torn sheet falling free; a drag too strong for a
// substep never reversing a point; the checks.

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

static void OpenScaled(const mvfxEmitterDef* emitter, const float* velocity, float scale)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.scale = scale;
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

static void Open(const mvfxEmitterDef* emitter, const float* velocity)
{
    OpenScaled(emitter, velocity, 1.0f);
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

static void TestLarge(void)
{
    // At the largest spacing and scale, edges of 10^12 whose cross
    // product's squares pass binary32's largest: thrown along its normal
    // with a tangent drag alone, the sheet keeps its speed.
    mvfxEmitterDef emitter = Sheet(0.0f, 1.0f);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.spacing = MVFX_MAX_EXTENT;
    static const float thrown[3] = {0, -5, 0};
    OpenScaled(&emitter, thrown, MVFX_MAX_SCALE);
    Steps(2);
    double mean[3];
    Mean(mean);
    CHECK(mean[1] == -5.0, "the largest, a tangent drag alone");
    mvfxDestroyContext(s_scene.context);
}

// Moves the sheet's first point to 10^29 along x and the first of its
// second row to 10^29 along z once, within MVFX_MAX_PLACE.
static void MoveFar(const mvfxModuleCall* call, void* user)
{
    bool* done = user;
    if (!*done && call->particles.count > 4)
    {
        call->particles.position[0][0] = 1e29f;
        call->particles.position[2][4] = 1e29f;
        *done = true;
    }
}

static void TestFar(void)
{
    // Two points moved 10^29 apart across the sheet: the triangle between
    // them and the second's neighbor has an area past binary32's largest,
    // its normal still along y. Thrown along y
    // with a tangent drag alone, the sheet keeps its speed over the step
    // after the move: a zero coefficient takes no share of an infinite
    // area. (The next step's host module call kills the points, their
    // speeds past MVFX_MAX_SPEED.)
    mvfxEmitterDef emitter = Sheet(0.0f, 1.0f);
    emitter.motion.gravity[1] = 0.0f;
    static bool moved;
    moved = false;
    static const mvfxHostModule mover = {
        .stage = mvfx_stageBeforeMotion, .function = MoveFar, .user = &moved};
    emitter.hostModules = &mover;
    emitter.hostModuleCount = 1;
    static const float thrown[3] = {0, -5, 0};
    Open(&emitter, thrown);
    Steps(2);
    double mean[3];
    Mean(mean);
    CHECK(moved && fabs(mean[1] + 5.0) <= 1e-3, "moved far, a tangent drag alone");
    mvfxDestroyContext(s_scene.context);
}

// Moves the sheet's second point to 10^20 along x once, within
// MVFX_MAX_PLACE.
static void MoveSecond(const mvfxModuleCall* call, void* user)
{
    bool* done = user;
    if (!*done && call->particles.count > 1)
    {
        call->particles.position[0][1] = 1e20f;
        *done = true;
    }
}

static void TestLongEdge(void)
{
    // The sheet's second point moved 10^20 along x, one substep a step:
    // the first point's one triangle, with the second and the first of the
    // second row, has an edge past 2^63 and one a spacing long, so it is
    // crossed scaled and its area, about 2.5e19, scaled back. Thrown along
    // its normal at 5 with a normal drag of 1, the triangle takes its whole
    // sixth: the first point leaves the step at -5 + 5/6. (The
    // constraints, as soft as they go, move the points only in the
    // sheet's plane.)
    mvfxEmitterDef emitter = Sheet(1.0f, 0.0f);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.substeps = 1;
    emitter.unit.stretchCompliance = MVFX_MAX_EXTENT;
    emitter.unit.shearCompliance = MVFX_MAX_EXTENT;
    emitter.unit.bendCompliance = MVFX_MAX_EXTENT;
    static bool moved;
    moved = false;
    static const mvfxHostModule mover = {
        .stage = mvfx_stageBeforeMotion, .function = MoveSecond, .user = &moved};
    emitter.hostModules = &mover;
    emitter.hostModuleCount = 1;
    static const float thrown[3] = {0, -5, 0};
    Open(&emitter, thrown);
    Steps(2);
    double mean[3];
    Mean(mean);
    float first = s_scene.records[0].velocity[1];
    CHECK(moved && fabsf(first + 5.0f * 5.0f / 6.0f) <= 1e-4f, "a long edge and a short one");
    mvfxDestroyContext(s_scene.context);
}

static void TestLift(void)
{
    // At 45 degrees (normal (1, 1, 0) / sqrt 2) in a wind of 10 along x,
    // no gravity: pushed along its normal, as much up (lift) as along the
    // wind (drag). The wind's own pull is a millionth: it moves the air. A
    // constant push along z moves no air.
    mvfxEmitterDef emitter = Sheet(0.5f, 0.0f);
    emitter.motion.gravity[1] = 0.0f;
    emitter.unit.direction[0] = 0.0f;
    emitter.unit.direction[2] = 1.0f;
    emitter.unit.downDirection[0] = 1.0f;
    emitter.unit.downDirection[1] = -1.0f;
    emitter.unit.downDirection[2] = 0.0f;
    static const mvfxForce forces[2] = {
        {.kind = mvfx_forceWind, .vector = {10, 0, 0}, .strength = 1e-6f},
        {.kind = mvfx_forceConstant, .vector = {0, 0, 1}, .strength = 0.3f}};
    emitter.forces = forces;
    emitter.forceCount = 2;
    Open(&emitter, NULL);
    Steps(20);
    double mean[3];
    Mean(mean);
    CHECK(mean[0] > 0.5 && fabs(mean[1] - mean[0]) <= 0.01 * mean[0] &&
              fabs(mean[2] - 0.3 * 20.0 / 60.0) <= 0.01,
          "pushed along its normal");
    mvfxDestroyContext(s_scene.context);
}

// Kicks each point of a four by four sheet once away from its middle,
// thirty a second per spacing across along x and down along z, so every
// two neighbors part.
static void KickApart(const mvfxModuleCall* call, void* user)
{
    bool* done = user;
    for (int32_t i = 0; !*done && i < call->particles.count; ++i)
    {
        uint32_t s = call->particles.serial[i];
        call->particles.velocity[0][i] += 30.0f * ((float)(s % 4u) - 1.5f);
        call->particles.velocity[2][i] += 30.0f * ((float)(s / 4u) - 1.5f);
    }
    *done = *done || call->particles.count > 0;
}

static void TestTorn(void)
{
    // Every edge torn: the sheet made stretchy (tearing reads the stretch
    // the projections leave), soft in shear and bending (rigid, they would
    // hold a stretched edge's ends together), and each point kicked away
    // from its neighbors, enough of its edges tear in its first step that
    // no quad is left: a free fall, the kicks being across it.
    mvfxEmitterDef emitter = Sheet(1.0f, 1.0f);
    emitter.unit.stretchCompliance = 0.01f;
    emitter.unit.shearCompliance = 1.0f;
    emitter.unit.bendCompliance = 1.0f;
    emitter.unit.tearStrain = 0.05f;
    static bool kicked;
    kicked = false;
    mvfxHostModule module = {
        .stage = mvfx_stageBeforeMotion, .function = KickApart, .user = &kicked};
    emitter.hostModules = &module;
    emitter.hostModuleCount = 1;
    Open(&emitter, NULL);
    Steps(2);
    mvfxEmitterStats stats = {0};
    static Record vertices[MOST];
    static uint32_t indices[6 * MOST];
    mvfxExpandBuffers buffers = {.vertices = vertices,
                                 .vertexCapacity = MOST,
                                 .indices = indices,
                                 .indexCapacity = 6 * MOST};
    int64_t vertexCount = 0;
    int64_t indexCount = -1;
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
                  mvfx_success &&
              stats.torn > 0 &&
              mvfxInstance_ExpandSheets(s_scene.context, s_scene.instance, 0, s_scene.layout,
                                        &s_view, &buffers, &vertexCount,
                                        &indexCount) == mvfx_success &&
              vertexCount == 16 && indexCount == 0,
          "torn until no quad is left");
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
        // Past MVFX_MAX_EXTENT is past a named bound.
        mvfxResult want = rule % 3 == 2 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect, NULL) == want;
    }
    CHECK(refused == 6, "every check");
    emitter = Sheet(MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "the bounds");
    emitter = Sheet(-1.0f, NAN);
    emitter.unit.kind = mvfx_unitChain;
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "a chain's unread");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestTerminal();
    TestLift();
    TestLarge();
    TestFar();
    TestLongEdge();
    TestTorn();
    TestHeld();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
