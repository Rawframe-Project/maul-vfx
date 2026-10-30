// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned water surfaces: a planned instance's plan of a surface, a CPU
// instance and a planned one stepped side by side: the host's pushes in
// the surface's frame of a moved and scaled instance; the waves'
// substeps, slowed past the most, as water.h counts them; shallow water's
// terrain resolved into its surface's frame (its own in either space, or
// the host's), its first step one substep, its later ones counted from
// the speed reported as water.h counts them (the most before a report),
// its sources' depths while it plays; in a split frame the pushes and
// clear in the first step alone; a surface's view; a stopped instance
// running while its surface moves, as its simulation reports; an
// emitter's coupling to a surface against the instance's transform; the
// counts reported once a tick, a clear emptying them, and the steps
// slowed counted from the plans; the refusals, and reads of a planned
// surface refused. That the CPU path counts its own substeps from its own
// speed the same way is left to the GPU part's harness, which compares
// their results; the reach and the reported speed are compared with the
// CPU path's in test_plan_water_internal.c, and the terrain with the CPU
// path's resolver there too.

#include "plan_oracle.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)

static mvfxContext* s_context;
static mvfxInstanceId s_cpu;
static mvfxInstanceId s_planned;
static mvfxEffectId s_effect;

// A context with an effect of the surfaces (and the emitters and gases,
// when given), a CPU instance and a planned one of it, both at origin
// scaled by scale.
static void Open(const mvfxWaterDef* waters, int32_t count, const mvfxEmitterDef* emitters,
                 int32_t emitterCount, const mvfxGasDef* gases, int32_t gasCount,
                 const double origin[3], float scale)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.waters = waters;
    def.waterCount = count;
    def.emitters = emitters;
    def.emitterCount = emitterCount;
    def.gases = gases;
    def.gasCount = gasCount;
    CHECK(mvfxCreateEffect(s_context, &def, &s_effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_effect;
    memcpy(instanceDef.origin, origin, sizeof instanceDef.origin);
    instanceDef.scale = scale;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_planned) == mvfx_success,
          "a planned instance");
}

static void NewContext(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
}

// The substeps water.h gives a step of seconds whose substeps are at
// most bound (MVFX_MAX_WATER_SUBSTEPS): equal ones, at least one, none
// for no time; past the most, the most of bound, slowed.
static int32_t Substeps(double seconds, double bound, double* dt, bool* slowed)
{
    *slowed = false;
    *dt = 0.0;
    if (!(seconds > 0.0))
    {
        return 0;
    }
    double n = fmax(ceil(seconds / bound), 1.0);
    if (n > (double)MVFX_MAX_WATER_SUBSTEPS)
    {
        *slowed = true;
        *dt = bound;
        return MVFX_MAX_WATER_SUBSTEPS;
    }
    *dt = seconds / n;
    return (int32_t)n;
}

// Whether a plan's substeps are those of a step of ticks at the bound.
static bool SubstepsAre(const mvfxWaterPlan* plan, int64_t ticks, double bound)
{
    double dt = 0.0;
    bool slowed = false;
    int32_t n = Substeps((double)ticks / (double)MVFX_TICKS_PER_SECOND, bound, &dt, &slowed);
    return plan->substeps == n && plan->slowed == slowed &&
           fabs((double)plan->dt - dt) <= 1e-6 * dt && plan->ticks == ticks;
}

// The clock after the planned instance's last step: its plan's end.
static int64_t PlannedClock(void)
{
    static mvfxWaterPlan plan;
    CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
    return plan.start + plan.ticks;
}

static mvfxWaterInfo Info(mvfxInstanceId instance)
{
    mvfxWaterInfo info;
    CHECK(mvfxInstance_GetWaterInfo(s_context, instance, 0, &info) == mvfx_success, "info");
    return info;
}

static void TestPushes(void)
{
    // An instance at (1, 0, 0) scaled by 2: a push at (2, 0, 3) of radius
    // 0.5 and depth 0.2 lies at (0.5, 1.5) in the surface's frame, radius
    // 0.25 and depth 0.1 there; the next step has none.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 32;
    water.size[1] = 32;
    water.cellSize = 0.1f;
    const double origin[3] = {1.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 2.0f);
    const mvfxWaterPush push = {.point = {2.0, 0.0, 3.0}, .radius = 0.5f, .depth = 0.2f};
    CHECK(mvfxInstance_PushWater(s_context, s_planned, 0, &push, 1) == mvfx_success &&
              mvfxInstance_PushWater(s_context, s_planned, 0, &push, 1) == mvfx_success,
          "pushed twice");
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success, "stepped");
    static mvfxWaterPlan plan;
    CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
    bool mapped = plan.pushCount == 2;
    for (int32_t p = 0; mapped && p < 2; ++p)
    {
        mapped = fabsf(plan.pushes[p].at[0] - 0.5f) <= 1e-6f &&
                 fabsf(plan.pushes[p].at[1] - 1.5f) <= 1e-6f &&
                 fabsf(plan.pushes[p].radius - 0.25f) <= 1e-7f &&
                 fabsf(plan.pushes[p].depth - 0.1f) <= 1e-7f;
    }
    CHECK(mapped, "the pushes in the surface's frame, in order");
    CHECK(plan.volumeScale == 8.0, "the volume's scale, the scale cubed");
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
              plan.pushCount == 0,
          "dropped with the plan");
    mvfxDestroyContext(s_context);
}

static void TestWaves(void)
{
    // A push on both twins in a step of no time (no substep): the CPU
    // path's far cells rise by what the pushes sank over the cells, which
    // the plan's sum gives bit for bit. Then a frame's step and a
    // second's: the waves' substeps as water.h bounds them (each at most
    // 0.9 h / (c sqrt(2))), the second slowed; no source depths or
    // terrain.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 16;
    water.size[1] = 16;
    water.cellSize = 0.05f;
    water.waveSpeed = 4.0f;
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 1.0f);
    const mvfxWaterPush push = {.point = {0.1, 0.0, -0.05}, .radius = 0.2f, .depth = 0.1f};
    CHECK(mvfxInstance_PushWater(s_context, s_cpu, 0, &push, 1) == mvfx_success &&
              mvfxInstance_PushWater(s_context, s_planned, 0, &push, 1) == mvfx_success &&
              mvfxContext_StepTicks(s_context, 0) == mvfx_success,
          "pushed");
    static mvfxWaterPlan sunk;
    CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &sunk) == mvfx_success &&
              sunk.pushCount == 1 && sunk.substeps == 0,
          "a plan of no time");
    const mvfxWaterRead read = {
        .field = mvfx_waterHeight, .format = mvfx_formatFloat32, .size = {1, 1}};
    float far = 0.0f;
    CHECK(mvfxInstance_ReadWater(s_context, s_cpu, 0, &read, &far, sizeof far) == mvfx_success,
          "read");
    CHECK(sunk.pushSank > 0.0 && far == (float)(sunk.pushSank / (16.0 * 16.0)),
          "the pushes' sum, the CPU path's lift");
    const int64_t steps[2] = {STEP_TICKS, MVFX_TICKS_PER_SECOND};
    for (int32_t k = 0; k < 2; ++k)
    {
        CHECK(mvfxContext_StepTicks(s_context, steps[k]) == mvfx_success, "stepped");
        static mvfxWaterPlan plan;
        CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
        double bound = 0.9 * (double)water.cellSize / ((double)water.waveSpeed * sqrt(2.0));
        CHECK(SubstepsAre(&plan, steps[k], bound) && !plan.enableTerrain &&
                  plan.sourceDepths[0] == 0.0f,
              "the waves' substeps");
        CHECK(plan.slowed == (k == 1), "a second's step slowed");
    }
    // The planned twin counts its slowed step as the CPU twin does.
    CHECK(Info(s_planned).stepsSlowed == 1 && Info(s_cpu).stepsSlowed == 1,
          "the steps slowed counted on both");
    mvfxDestroyContext(s_context);
}

// A heightfield of the instance's frame of 33 by 33 points over 4 by 4
// about the origin, sloping from -0.5 at -X to 0.5 at +X.
static mvfxCollider Slope(void)
{
    static float heights[33 * 33];
    for (int32_t i = 0; i < 33 * 33; ++i)
    {
        heights[i] = 0.25f * (-2.0f + (float)(i % 33) / 8.0f);
    }
    mvfxGridDef grid = mvfxDefaultGridDef();
    grid.size[0] = 33;
    grid.size[1] = 1;
    grid.size[2] = 33;
    grid.values = heights;
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(s_context, &grid, &id) == mvfx_success, "grid");
    return (mvfxCollider){.kind = mvfx_colliderHeightfield,
                          .space = 1,
                          .halfExtents = {2.0f, 1.0f, 2.0f},
                          .rotation = {0, 0, 0, 1},
                          .grid = id};
}

// A shallow lake of 40 by 40 cells of 0.1 filled to 0 over the slope, a
// spring at its middle.
static mvfxWaterDef Lake(void)
{
    static const mvfxWaterSource spring = {
        .shape = mvfx_gasSphere, .center = {0.0f, 0.0f, 0.0f}, .radius = 0.3f, .rate = 0.25f};
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.model = mvfx_waterShallow;
    water.size[0] = 40;
    water.size[1] = 40;
    water.cellSize = 0.1f;
    water.terrain = Slope();
    water.enableTerrain = true;
    water.sources = &spring;
    water.sourceCount = 1;
    return water;
}

static void TestShallow(void)
{
    // Each step after the first, the planned twin told a speed at the
    // step's start, as its simulation would report: its substeps of at
    // most half a cell at that speed (water.h, mvfx_waterShallow), the
    // most past them, slowed; the first step one substep (counted before
    // the fill); its terrain; the source's depth while playing, none after
    // a stop; the steps slowed counted from the plans.
    NewContext();
    mvfxWaterDef water = Lake();
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 1.0f);
    int32_t wrong = 0;
    int32_t split = 0;
    uint64_t slowed = 0;
    int64_t clock = 0;
    for (int32_t s = 0; s < 40; ++s)
    {
        if (s == 30)
        {
            CHECK(mvfxInstance_Stop(s_context, s_cpu, mvfx_stopEmitting) == mvfx_success &&
                      mvfxInstance_Stop(s_context, s_planned, mvfx_stopEmitting) == mvfx_success,
                  "stopped");
        }
        // Rising, and past the most from step 35.
        float speed = s < 35 ? 0.37f + 0.29f * (float)s : 100.0f;
        CHECK(s == 0 ||
                  mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, clock, speed) == mvfx_success,
              "reported");
        bool playing = s < 30;
        CHECK(mvfxContext_StepTicks(s_context, 4 * STEP_TICKS) == mvfx_success, "stepped");
        static mvfxWaterPlan plan;
        CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
        double bound = s == 0 ? (double)INFINITY : 0.5 * (double)water.cellSize / (double)speed;
        float depth = playing ? (float)(0.25 * (double)plan.ticks / MVFX_TICKS_PER_SECOND) : 0.0f;
        wrong += !SubstepsAre(&plan, 4 * STEP_TICKS, bound) || plan.sourceDepths[0] != depth;
        wrong += !plan.enableTerrain || plan.terrain.grid.index1 != water.terrain.grid.index1;
        split += plan.substeps > 1;
        slowed += plan.slowed ? 1u : 0u;
        clock = plan.start + plan.ticks;
    }
    CHECK(wrong == 0, "each step's plan as water.h counts it from the speed reported");
    CHECK(split > 0 && slowed == 5, "some steps split, those past the most slowed");
    CHECK(Info(s_planned).stepsSlowed == slowed, "the steps slowed counted from the plans");
    mvfxDestroyContext(s_context);
}

static void TestTerrain(void)
{
    // Three lakes, their grids off the instance's origin: one on the
    // slope of the instance's frame, one on a turned heightfield of the
    // world's, and one with hostTerrain on the instance's first
    // heightfield collider (after a plane), under a moving, turning,
    // growing instance. Each plan's terrain is its collider resolved into
    // the surface's frame where the step ends (plan.h, mvfxWaterPlan's
    // terrain; water.h, terrain), the frame mvfxInstance_GetWaterInfo
    // places: its box mapped, its turn composed, scaled, grown by no
    // radius, its grid kept.
    NewContext();
    static mvfxWaterDef waters[3];
    for (int32_t w = 0; w < 3; ++w)
    {
        waters[w] = Lake();
        waters[w].center[0] = 0.3f * (float)w;
        waters[w].center[2] = -0.2f;
    }
    waters[1].terrain.space = 0;
    waters[1].terrain.center[0] = 1.5f;
    waters[1].terrain.center[1] = -0.25f;
    waters[1].terrain.rotation[1] = sinf(0.2f);
    waters[1].terrain.rotation[3] = cosf(0.2f);
    waters[2].enableTerrain = false;
    waters[2].hostTerrain = true;
    mvfxCollider hosts[2] = {
        {.kind = mvfx_colliderPlane, .center = {0, -1, 0}, .normal = {0, 1, 0}}, waters[1].terrain};
    hosts[1].center[2] = 0.75f;
    const mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(waters, 3, &emitter, 1, NULL, 0, origin, 1.0f);
    CHECK(mvfxInstance_SetColliders(s_context, s_planned, hosts, 2) == mvfx_success, "colliders");
    const mvfxCollider* defs[3] = {&waters[0].terrain, &waters[1].terrain, &hosts[1]};
    int32_t wrong = 0;
    for (int32_t s = 0; s < 6; ++s)
    {
        const double at[3] = {2.0 + 0.5 * s, 0.1 * s, -1.0};
        const float rotation[4] = {0.0f, sinf(0.05f * (float)s), 0.0f, cosf(0.05f * (float)s)};
        CHECK(mvfxInstance_SetTransform(s_context, s_planned, at, rotation, 1.0f + 0.1f * (float)s,
                                        false) == mvfx_success &&
                  mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success,
              "moved and stepped");
        static mvfxEmitterPlan emitterPlan;
        CHECK(mvfxInstance_GetEmitterPlan(s_context, s_planned, 0, &emitterPlan) == mvfx_success,
              "the instance's place");
        for (int32_t w = 0; w < 3; ++w)
        {
            static mvfxWaterPlan plan;
            mvfxWaterInfo surface;
            CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, w, &plan) == mvfx_success &&
                      mvfxInstance_GetWaterInfo(s_context, s_planned, w, &surface) == mvfx_success,
                  "a plan and its surface");
            const mvfxCollider* def = defs[w];
            Frame frame = SurfaceFrame(&surface, &emitterPlan.to, def->space);
            wrong += !plan.enableTerrain || !SameCollider(&plan.terrain, def, true, &frame, 0.0) ||
                     plan.terrain.grid.index1 != def->grid.index1;
        }
    }
    CHECK(wrong == 0, "each terrain resolved into its surface's frame");
    mvfxDestroyContext(s_context);
}

static void TestUnreported(void)
{
    // A planned lake told nothing: its first step one substep (no water
    // before its fill), each later the most, not slowed; a restart counts
    // from no water again, and its next step the most again, its report
    // forgotten.
    NewContext();
    mvfxWaterDef water = Lake();
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 1.0f);
    static mvfxWaterPlan plan;
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
              plan.substeps == 1 && plan.cleared,
          "the first step one substep, cleared");
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
              plan.substeps == MVFX_MAX_WATER_SUBSTEPS && !plan.slowed &&
              plan.dt ==
                  (float)((double)STEP_TICKS / MVFX_TICKS_PER_SECOND / MVFX_MAX_WATER_SUBSTEPS) &&
              !plan.cleared,
          "unreported, the most");
    CHECK(mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, PlannedClock(), 1.0f) ==
                  mvfx_success &&
              mvfxInstance_Restart(s_context, s_planned) == mvfx_success &&
              mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
              plan.substeps == 1 && plan.cleared,
          "a restart counts from no water");
    // Told 1, the step would take one substep; forgotten, the most.
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
              plan.substeps == MVFX_MAX_WATER_SUBSTEPS,
          "its report forgotten");
    mvfxDestroyContext(s_context);
}

static void TestSplit(void)
{
    // Beside a liquid gas of a courant number of 0.05 every frame splits:
    // the whole plan refused, each step's its own, the pushes and the
    // clear in the first.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 8;
    water.size[1] = 8;
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.closedWalls = 63;
    gas.courant = 0.05f;
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 64;
    emitter.motion.gravity[1] = -9.81f;
    emitter.liquid.enable = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = 0.05f;
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, &emitter, 1, &gas, 1, origin, 1.0f);
    const mvfxWaterPush push = {.point = {0.0, 0.0, 0.0}, .radius = 0.2f, .depth = 0.1f};
    CHECK(mvfxInstance_PushWater(s_context, s_planned, 0, &push, 1) == mvfx_success, "pushed");
    int32_t steps = 0;
    static mvfxWaterPlan plan;
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetPlanSteps(s_context, s_planned, &steps) == mvfx_success && steps > 1,
          "a split frame");
    CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_errorState,
          "the whole plan refused");
    int32_t wrong = 0;
    int64_t at = 0;
    for (int32_t s = 0; s < steps; ++s)
    {
        CHECK(mvfxInstance_GetWaterStepPlan(s_context, s_planned, 0, s, &plan) == mvfx_success,
              "a step's plan");
        wrong += plan.pushCount != (s == 0 ? 1 : 0) || plan.cleared != (s == 0) || plan.start != at;
        at = plan.start + plan.ticks;
    }
    CHECK(wrong == 0 && at == STEP_TICKS, "the pushes and clear in the first, spans in turn");
    mvfxDestroyContext(s_context);
}

static void TestHolds(void)
{
    // A planned surface moves until its simulation says it is still: a
    // stopped instance runs on; a still report while a coupled emitter's
    // particles live is ignored, after their last death it counts, and a
    // moving one always counts.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 8;
    water.size[1] = 8;
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 600.0f;
    emitter.lifetime = mvfxRangeSource(0.5f, 0.5f);
    emitter.water = (mvfxWaterCoupling){.enable = true, .water = 0, .radius = 0.05f};
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, &emitter, 1, NULL, 0, origin, 1.0f);
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_Stop(s_context, s_planned, mvfx_stopEmitting) == mvfx_success &&
              mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success,
          "stopped");
    int64_t clock = PlannedClock();
    CHECK(mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock, false) == mvfx_success &&
              !mvfxInstance_IsFinished(s_context, s_planned),
          "still ignored while its particles live");
    for (int32_t s = 0; s < 40; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success, "stepped");
    }
    clock = PlannedClock();
    CHECK(!mvfxInstance_IsFinished(s_context, s_planned) &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock, true) == mvfx_success &&
              !mvfxInstance_IsFinished(s_context, s_planned) &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock, false) == mvfx_success &&
              mvfxInstance_IsFinished(s_context, s_planned),
          "still after the last death, finished");
    uint64_t misuse = mvfxContext_GetMisuse(s_context);
    CHECK(mvfxInstance_SetWaterHolds(NULL, s_planned, 0, 0, true) == mvfx_errorInvalid &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 1, 0, true) == mvfx_errorInvalid &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock + 1, true) ==
                  mvfx_errorInvalid &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, -1, true) == mvfx_errorInvalid &&
              mvfxContext_GetMisuse(s_context) == misuse + 3 &&
              mvfxInstance_SetWaterHolds(s_context, s_cpu, 0, 0, true) == mvfx_errorUnsupported &&
              mvfxInstance_SetWaterHolds(s_context, (mvfxInstanceId){0}, 0, 0, true) ==
                  mvfx_errorStale,
          "the holds setter's refusals");
    mvfxDestroyContext(s_context);
}

static void TestCoupling(void)
{
    // A world emitter coupled to a shallow surface (a pour's) of a moved,
    // turned and scaled instance: its plan's map (plan.h,
    // mvfxPlannedWaterCoupling) takes the surface's frame into the
    // particles' space by the instance where the step starts (water.h:
    // the grid's center in the instance's frame), less the anchor the
    // particles are offsets from; its up the surface's Y turned; its
    // radius and push in the surface's lengths; its gravity's strength in
    // them; the coupling's numbers its def's; an emitter without a
    // coupling plans none.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 8;
    water.size[1] = 8;
    water.center[1] = 0.3f;
    water.model = mvfx_waterShallow;
    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    emitters[0].space = mvfx_spaceWorld;
    emitters[0].water = (mvfxWaterCoupling){.enable = true,
                                            .water = 0,
                                            .radius = 0.05f,
                                            .push = 0.04f,
                                            .pour = true,
                                            .buoyancy = 2.0f,
                                            .drag = 1.5f};
    emitters[0].motion.gravity[1] = -9.81f;
    const double origin[3] = {1.0, 0.0, -2.0};
    Open(&water, 1, emitters, 2, NULL, 0, origin, 2.0f);
    const double moved[3] = {1.5, 0.25, -2.0};
    const float turn[4] = {0.0f, 0.38268343f, 0.0f, 0.92387953f};
    mvfxEmitterPlan plan;
    mvfxEmitterPlan none;
    bool planned =
        mvfxInstance_SetTransform(s_context, s_planned, moved, turn, 2.0f, false) == mvfx_success &&
        mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
        mvfxInstance_GetEmitterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
        mvfxInstance_GetEmitterPlan(s_context, s_planned, 1, &none) == mvfx_success;
    CHECK(planned, "a coupled emitter planned");
    if (!planned)
    {
        mvfxDestroyContext(s_context);
        return;
    }
    const mvfxPlannedWaterCoupling* c = &plan.water;
    const mvfxPlanPlace* at = &plan.from;
    const double q[4] = {(double)at->rotation[0], (double)at->rotation[1], (double)at->rotation[2],
                         (double)at->rotation[3]};
    double scale = (double)at->scale;
    const double center[3] = {(double)water.center[0] * scale, (double)water.center[1] * scale,
                              (double)water.center[2] * scale};
    double turned[3];
    Turn(q, center, turned);
    static const double y[3] = {0.0, 1.0, 0.0};
    double up[3];
    Turn(q, y, up);
    bool mapped = c->water == 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double placed = at->origin[k] + turned[k] - plan.anchorAfter[k];
        mapped = mapped && fabs((double)c->origin[k] - placed) <= 1e-5 &&
                 fabs((double)c->up[k] - up[k]) <= 1e-6;
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        mapped = mapped && fabs((double)c->rotation[k] - q[k]) <= 1e-6;
    }
    const float* g = emitters[0].motion.gravity;
    const double pull[3] = {(double)g[0], (double)g[1], (double)g[2]};
    double gravity = sqrt(pull[0] * pull[0] + pull[1] * pull[1] + pull[2] * pull[2]);
    CHECK(mapped && fabs((double)c->sigma - scale) <= 1e-6 &&
              fabs((double)c->radius - 0.05 / scale) <= 1e-7 &&
              fabs((double)c->push - 0.04 / scale) <= 1e-7 &&
              fabs((double)c->strength - gravity / scale) <= 1e-5 && c->drag == 1.5f &&
              c->buoyancy == 2.0f && c->pour && none.water.water == -1,
          "the planned coupling");
    mvfxDestroyContext(s_context);
}

static void TestView(void)
{
    // A lake's view: its def, its source the effect's copy, its cells and
    // faces; waves have no faces; refusals.
    NewContext();
    mvfxWaterDef waters[2] = {Lake(), mvfxDefaultWaterDef()};
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(waters, 2, NULL, 0, NULL, 0, origin, 1.0f);
    mvfxEffectId effect = s_effect;
    mvfxWaterView view;
    CHECK(mvfxEffect_GetWaterView(s_context, effect, 0, &view) == mvfx_success &&
              view.def.model == mvfx_waterShallow && view.cells == 1600 &&
              view.faces[0] == 41 * 40 && view.faces[1] == 40 * 41 && view.def.sourceCount == 1 &&
              view.def.sources != NULL && view.def.sources != waters[0].sources &&
              view.def.sources[0].rate == 0.25f && view.def.enableTerrain &&
              view.def.terrain.grid.index1 == waters[0].terrain.grid.index1,
          "a lake's view");
    CHECK(mvfxEffect_GetWaterView(s_context, effect, 1, &view) == mvfx_success &&
              view.cells ==
                  (int64_t)mvfxDefaultWaterDef().size[0] * mvfxDefaultWaterDef().size[1] &&
              view.faces[0] == 0 && view.def.sources == NULL,
          "waves' view");
    CHECK(mvfxEffect_GetWaterView(NULL, effect, 0, &view) == mvfx_errorInvalid &&
              mvfxEffect_GetWaterView(s_context, effect, 2, &view) == mvfx_errorInvalid &&
              mvfxEffect_GetWaterView(s_context, effect, 0, NULL) == mvfx_errorInvalid &&
              mvfxEffect_GetWaterView(s_context, (mvfxEffectId){0}, 0, &view) == mvfx_errorStale &&
              view.cells == 0,
          "the view's refusals");
    mvfxDestroyContext(s_context);
}

static void TestCounts(void)
{
    // A planned lake's counts: the reach replaced and the counts added once
    // a tick, an older or repeated report ignored; a stop that clears
    // empties them and ignores the reports before it; the refusals.
    NewContext();
    mvfxWaterDef water = Lake();
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 1.0f);
    CHECK(mvfxContext_StepTicks(s_context, 2 * STEP_TICKS) == mvfx_success, "stepped");
    mvfxWaterInfo info;
    bool counted = mvfxInstance_SetWaterCounts(s_context, s_planned, 0, STEP_TICKS, 0.5f, 3,
                                               0.25) == mvfx_success &&
                   mvfxInstance_SetWaterCounts(s_context, s_planned, 0, STEP_TICKS, 0.7f, 3,
                                               0.25) == mvfx_success &&
                   mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 2 * STEP_TICKS, 0.2f, 4,
                                               0.5) == mvfx_success &&
                   mvfxInstance_SetWaterCounts(s_context, s_planned, 0, STEP_TICKS, 0.9f, 9, 9.0) ==
                       mvfx_success &&
                   mvfxInstance_GetWaterInfo(s_context, s_planned, 0, &info) == mvfx_success &&
                   info.facesHeld == 7 && info.volumePoured == 0.75;
    CHECK(counted, "each tick's report counted once");
    CHECK(mvfxInstance_Stop(s_context, s_planned, mvfx_stopAndClear) == mvfx_success &&
              mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 2 * STEP_TICKS, 1.0f, 5, 1.0) ==
                  mvfx_success &&
              mvfxInstance_GetWaterInfo(s_context, s_planned, 0, &info) == mvfx_success &&
              info.facesHeld == 0 && info.volumePoured == 0.0,
          "a clear empties them, the reports before it ignored");
    uint64_t misuse = mvfxContext_GetMisuse(s_context);
    bool refused =
        mvfxInstance_SetWaterCounts(NULL, s_planned, 0, 0, 0.0f, 0, 0.0) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 1, 0, 0.0f, 0, 0.0) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 0, -1, 0.0f, 0, 0.0) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 3 * STEP_TICKS, 0.0f, 0, 0.0) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 0, -1.0f, 0, 0.0) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 0, INFINITY, 0, 0.0) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 0, 0.0f, 0, (double)NAN) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 0, 0.0f, 0, -1.0) ==
            mvfx_errorInvalid &&
        mvfxContext_GetMisuse(s_context) == misuse + 7 &&
        mvfxInstance_SetWaterCounts(s_context, s_cpu, 0, 0, 0.0f, 0, 0.0) ==
            mvfx_errorUnsupported &&
        mvfxInstance_SetWaterCounts(s_context, (mvfxInstanceId){0}, 0, 0, 0.0f, 0, 0.0) ==
            mvfx_errorStale;
    CHECK(refused, "the counts setter's refusals");
    mvfxDestroyContext(s_context);
}

static void TestRefusals(void)
{
    NewContext();
    mvfxWaterDef waters[2] = {Lake(), mvfxDefaultWaterDef()};
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(waters, 2, NULL, 0, NULL, 0, origin, 1.0f);
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success, "stepped");
    static mvfxWaterPlan plan;
    uint64_t misuse = mvfxContext_GetMisuse(s_context);
    bool refused =
        mvfxInstance_GetWaterPlan(NULL, s_planned, 0, &plan) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterPlan(s_context, s_planned, 0, NULL) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterPlan(s_context, s_planned, 2, &plan) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterPlan(s_context, s_cpu, 0, &plan) == mvfx_errorUnsupported &&
        mvfxInstance_GetWaterPlan(s_context, (mvfxInstanceId){0}, 0, &plan) == mvfx_errorStale &&
        mvfxInstance_GetWaterStepPlan(s_context, s_planned, 0, 1, &plan) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterStepPlan(s_context, s_planned, 0, -1, &plan) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterStepPlan(s_context, s_planned, -1, 0, &plan) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(NULL, s_planned, 0, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(s_context, s_planned, 1, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(s_context, s_planned, 2, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, -1, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, STEP_TICKS + 1, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, 0, -1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, 0, NAN) == mvfx_errorInvalid &&
        mvfxContext_GetMisuse(s_context) == misuse + 6 &&
        mvfxInstance_SetWaterSpeed(s_context, s_cpu, 0, 0, 1.0f) == mvfx_errorUnsupported &&
        mvfxInstance_SetWaterSpeed(s_context, (mvfxInstanceId){0}, 0, 0, 1.0f) == mvfx_errorStale;
    CHECK(refused, "the plans' and the speed setter's refusals");
    // An older report than the latest is ignored: the next step counts
    // from 2 a second, not 7.
    static mvfxWaterPlan next;
    CHECK(mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, STEP_TICKS, 2.0f) == mvfx_success &&
              mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, 0, 7.0f) == mvfx_success &&
              mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &next) == mvfx_success &&
              SubstepsAre(&next, STEP_TICKS, 0.5 * (double)waters[0].cellSize / 2.0) &&
              !SubstepsAre(&next, STEP_TICKS, 0.5 * (double)waters[0].cellSize / 7.0),
          "the latest report counts");
    // A planned surface's fields are its simulation's.
    const mvfxWaterRead read = {
        .field = mvfx_waterHeight, .format = mvfx_formatFloat32, .size = {1, 1}};
    float out = 0.0f;
    const double point[3] = {0.0, 0.0, 0.0};
    mvfxWaterSample sample;
    CHECK(mvfxInstance_ReadWater(s_context, s_planned, 0, &read, &out, sizeof out) ==
                  mvfx_errorUnsupported &&
              mvfxInstance_SampleWater(s_context, s_planned, 0, point, 1, &sample) ==
                  mvfx_errorUnsupported &&
              mvfxInstance_ReadWater(s_context, s_cpu, 0, &read, &out, sizeof out) == mvfx_success,
          "reads of a planned surface refused");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestCounts();
    TestPushes();
    TestWaves();
    TestShallow();
    TestTerrain();
    TestUnreported();
    TestSplit();
    TestHolds();
    TestCoupling();
    TestView();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
