// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Self-collision: a sheet thrown onto another kept a thickness above it,
// where without it they pass through; a chain's last point laid on its
// first parted, and a chain at rest thicker than its spacing left still
// (its neighbors filtered out at rest);
// friction slowing the sheets' sliding; a point's candidates capped and
// counted; the checks. One pass's mean of a point's pairs is tested in
// test_self_internal.c.

#include "test_harness.h"

#include "maul-vfx/instance.h"
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

// Kicks the points of serials from first on, once, before the motion of
// the step they are first seen in.
typedef struct Kick
{
    uint32_t first;
    float velocity[3];
    bool done;
} Kick;

static void KickOnce(const mvfxModuleCall* call, void* user)
{
    Kick* kick = user;
    for (int32_t i = 0; !kick->done && i < call->particles.count; ++i)
    {
        for (int32_t k = 0; call->particles.serial[i] >= kick->first && k < 3; ++k)
        {
            call->particles.velocity[k][i] += kick->velocity[k];
        }
    }
    kick->done = kick->done || call->particles.count > 0;
}

// Two four by four sheets lying flat, 0.5 apart, no gravity, a thickness
// of 0.2, eight substeps; the second kicked.
static mvfxEmitterDef Sheets(float thickness, Kick* kick)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 32;
    emitter.hostSpawnCapacity = 2;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                 .points = 4,
                                 .rows = 4,
                                 .spacing = 0.5f,
                                 .direction = {1, 0, 0},
                                 .downDirection = {0, 0, 1},
                                 .substeps = 8,
                                 .selfThickness = thickness,
                                 .selfContacts = 8};
    static mvfxHostModule module;
    module = (mvfxHostModule){.stage = mvfx_stageBeforeMotion, .function = KickOnce, .user = kick};
    emitter.hostModules = &module;
    emitter.hostModuleCount = 1;
    return emitter;
}

static void Open(const mvfxEmitterDef* emitter, const double* places, int32_t units)
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
    mvfxSpawnRequest request = {.count = units, .positions = places};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
}

static void Read(void)
{
    CHECK(mvfxInstance_WriteParticles(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                      NULL, 0, s_scene.records, MOST,
                                      &s_scene.count) == mvfx_success,
          "read");
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

static double Gap(int32_t a, int32_t b)
{
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = (double)s_scene.records[a].position[k] - (double)s_scene.records[b].position[k];
        square += d * d;
    }
    return sqrt(square);
}

static mvfxEmitterStats Stats(void)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    return stats;
}

static void TestSheets(void)
{
    // The upper sheet, 1 above, thrown down at 5 (and along x at slide):
    // with a thickness of 0.2 no point of it comes within 0.198 of a
    // point of the other; with none it passes through. Friction slows the
    // slide: the sheets' x speeds' difference after the hit is smaller
    // with friction 1 than with none (points are spheres on a grid, so
    // even without friction they trade some of it as they meet).
    static const double places[6] = {0, 0, 0, 0, 1, 0};
    double nearest[2] = {1e9, 1e9};
    double below = 0.0;
    double slide[2] = {0.0, 0.0};
    for (int32_t run = 0; run < 4; ++run)
    {
        static Kick kick;
        kick = (Kick){.first = 16, .velocity = {run >= 2 ? 2.0f : 0.0f, -5.0f, 0.0f}};
        mvfxEmitterDef emitter = Sheets(run == 1 ? 0.0f : 0.2f, &kick);
        emitter.unit.selfFriction = run == 3 ? 1.0f : 0.0f;
        Open(&emitter, places, 2);
        double least = 1e9;
        for (int32_t step = 0; step < 30; ++step)
        {
            Steps(1);
            Read();
            for (int32_t a = 0; a < 16; ++a)
            {
                for (int32_t b = 16; b < 32; ++b)
                {
                    double gap = Gap(a, b);
                    least = gap < least ? gap : least;
                }
            }
        }
        double sums[2] = {0.0, 0.0};
        for (int32_t i = 0; i < 32; ++i)
        {
            sums[i / 16] += (double)s_scene.records[i].velocity[0] / 16.0;
        }
        if (run < 2)
        {
            nearest[run] = least;
            below = run == 1 ? (double)s_scene.records[16].position[1] : below;
        }
        else
        {
            slide[run - 2] = sums[1] - sums[0];
        }
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(nearest[0] >= 0.198, "kept a thickness apart");
    CHECK(below < -0.5, "without, through");
    CHECK(fabs(slide[1]) < fabs(slide[0]) - 0.1, "friction slows the slide");
}

// Whether the next step lays a chain's last point at x 0.05, before its
// motion.
static bool s_lay;

static void Lay(const mvfxModuleCall* call, void* user)
{
    (void)user;
    if (s_lay && call->particles.count == 6)
    {
        call->particles.position[0][5] = 0.05f;
        s_lay = false;
    }
}

static void TestChain(void)
{
    // A chain of six along x, its points 0.5 apart, soft: its last point
    // laid 0.05 from its first is parted to 0.3 within a step. A chain at
    // rest with a thickness of 0.6, more than its spacing, keeps its
    // places: neighbors are filtered out, points two apart already clear.
    for (int32_t laid = 0; laid < 2; ++laid)
    {
        static Kick none;
        none = (Kick){.first = 1000};
        mvfxEmitterDef emitter = Sheets(laid ? 0.3f : 0.6f, &none);
        emitter.unit.kind = mvfx_unitChain;
        emitter.unit.points = 6;
        emitter.unit.stretchCompliance = 1.0f;
        emitter.unit.bendCompliance = 1.0f;
        emitter.capacity = 6;
        emitter.hostSpawnCapacity = 1;
        static const mvfxHostModule lay = {.stage = mvfx_stageBeforeMotion, .function = Lay};
        emitter.hostModules = &lay;
        s_lay = false;
        static const double root[3] = {0, 0, 0};
        Open(&emitter, root, 1);
        Steps(1);
        if (laid)
        {
            s_lay = true;
            Steps(1);
            Read();
            CHECK(Gap(0, 5) >= 0.297, "a fold parted");
        }
        else
        {
            Steps(10);
            Read();
            bool still = true;
            for (int32_t i = 0; i < 6; ++i)
            {
                still = still && fabsf(s_scene.records[i].position[0] - 0.5f * (float)i) <= 1e-5f;
            }
            CHECK(still, "neighbors filtered out");
        }
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestSheetAtRest(void)
{
    // A sheet at rest with a thickness of 0.6, more than its spacing:
    // neighbors across and down are filtered out, diagonals already clear,
    // so nothing moves.
    static const double root[3] = {0, 0, 0};
    static Kick none;
    none = (Kick){.first = 1000};
    mvfxEmitterDef emitter = Sheets(0.6f, &none);
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 1;
    Open(&emitter, root, 1);
    Steps(10);
    Read();
    bool still = s_scene.count == 16;
    for (int32_t i = 0; still && i < 16; ++i)
    {
        still = fabsf(s_scene.records[i].position[0] - 0.5f * (float)(i % 4)) <= 1e-5f &&
                fabsf(s_scene.records[i].position[2] - 0.5f * (float)(i / 4)) <= 1e-5f;
    }
    CHECK(still, "a sheet's neighbors filtered out");
    mvfxDestroyContext(s_scene.context);
}

static void TestCapped(void)
{
    // One candidate a point: the sheets' points near more are counted.
    static const double places[6] = {0, 0, 0, 0, 0.3, 0};
    static Kick none;
    none = (Kick){.first = 1000};
    mvfxEmitterDef emitter = Sheets(0.6f, &none);
    emitter.unit.selfContacts = 1;
    Open(&emitter, places, 2);
    Steps(2);
    CHECK(Stats().selfContactsCapped > 0, "capped");
    mvfxDestroyContext(s_scene.context);
    emitter.unit.selfContacts = MVFX_MAX_NEIGHBORS;
    Open(&emitter, places, 2);
    Steps(2);
    CHECK(Stats().selfContactsCapped == 0, "not capped");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static Kick none;
    mvfxEmitterDef emitter = Sheets(0.2f, &none);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 8; ++rule)
    {
        emitter = Sheets(0.2f, &none);
        mvfxUnitDef* u = &emitter.unit;
        switch (rule)
        {
        case 0:
            u->selfThickness = -0.1f;
            break;
        case 1:
            u->selfThickness = NAN;
            break;
        case 2:
            u->selfThickness = 2.0f * MVFX_MAX_EXTENT;
            break;
        case 3:
            u->selfFriction = -0.1f;
            break;
        case 4:
            u->selfFriction = 1.5f;
            break;
        case 5:
            u->selfFriction = NAN;
            break;
        case 6:
            u->selfContacts = 0;
            break;
        default:
            u->selfContacts = MVFX_MAX_NEIGHBORS + 1;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 2 || rule == 7 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect) == want;
    }
    CHECK(refused == 8, "every check");
    emitter = Sheets(MVFX_MAX_EXTENT, &none);
    emitter.unit.selfFriction = 1.0f;
    emitter.unit.selfContacts = MVFX_MAX_NEIGHBORS;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the bounds");
    emitter = Sheets(0.0f, &none);
    emitter.unit.selfFriction = NAN;
    emitter.unit.selfContacts = -4;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "none, unread");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestSheets();
    TestChain();
    TestSheetAtRest();
    TestCapped();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
