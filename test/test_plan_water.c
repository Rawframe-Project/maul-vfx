// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned water surfaces: a planned instance's plan of a surface is what
// the CPU path's step of it takes, a CPU instance and a planned one
// stepped side by side: the host's pushes in the surface's frame of a
// moved and scaled instance; the waves' substeps, slowed past the most;
// shallow water's terrain resolved, its first step one substep, its
// later ones counted from the speed reported as the CPU path counts from
// its own (the most before a report), its sources' depths while it
// plays; in a split frame the pushes and clear in the first step alone;
// a surface's view; a stopped instance running while its surface moves,
// as its simulation reports; an emitter's coupling to a surface as
// resolved; the counts and reach reported once a tick, a clear emptying
// them, and the steps slowed counted from the plans; the refusals, and
// reads of a planned surface refused.

#include "direction.h"
#include "force.h"
#include "instance.h"
#include "instance_api.h"
#include "plan_resolved.h"
#include "test_harness.h"
#include "water.h"
#include "water_ground.h"
#include "water_shallow.h"
#include "water_step.h"

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

static const mvfxInstance* Live(mvfxInstanceId instance)
{
    return mvfxContextInstance(s_context, instance);
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
    // second's: the waves' substeps as the CPU path counts them, the
    // second slowed; no source depths or terrain.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 16;
    water.size[1] = 16;
    water.cellSize = 0.05f;
    water.waveSpeed = 4.0f;
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 1.0f);
    const mvfxCompiledWater* compiled = &Live(s_planned)->effect->waters[0];
    const mvfxWaterPush push = {.point = {0.1, 0.0, -0.05}, .radius = 0.2f, .depth = 0.1f};
    CHECK(mvfxInstance_PushWater(s_context, s_cpu, 0, &push, 1) == mvfx_success &&
              mvfxInstance_PushWater(s_context, s_planned, 0, &push, 1) == mvfx_success &&
              mvfxContext_StepTicks(s_context, 0) == mvfx_success,
          "pushed");
    static mvfxWaterPlan sunk;
    CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &sunk) == mvfx_success &&
              sunk.pushCount == 1 && sunk.substeps == 0,
          "a plan of no time");
    const mvfxWaterState* cpu = &Live(s_cpu)->waters[0];
    float far = cpu->heights[cpu->current][0];
    CHECK(sunk.pushSank > 0.0 && far == (float)(sunk.pushSank / (double)compiled->cells),
          "the pushes' sum, the CPU path's lift");
    const int64_t steps[2] = {STEP_TICKS, MVFX_TICKS_PER_SECOND};
    for (int32_t k = 0; k < 2; ++k)
    {
        CHECK(mvfxContext_StepTicks(s_context, steps[k]) == mvfx_success, "stepped");
        static mvfxWaterPlan plan;
        CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
        float dt = 0.0f;
        bool slowed = false;
        int32_t count = mvfxWaveSubsteps(compiled, (double)steps[k] / (double)MVFX_TICKS_PER_SECOND,
                                         &dt, &slowed);
        CHECK(plan.substeps == count && plan.dt == dt && plan.slowed == slowed &&
                  plan.ticks == steps[k] && !plan.hasTerrain && plan.sourceDepths[0] == 0.0f,
              "the waves' substeps");
        CHECK(slowed == (k == 1), "a second's step slowed");
    }
    // The planned twin counts its slowed step as the CPU twin does.
    CHECK(Live(s_planned)->waters[0].stepsSlowed == 1 && Live(s_cpu)->waters[0].stepsSlowed == 1,
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
    water.hasTerrain = true;
    water.sources = &spring;
    water.sourceCount = 1;
    return water;
}

static void TestShallow(void)
{
    // Each step after the first, the planned twin told the CPU twin's
    // speed at the step's start: the same speed kept, so the same split,
    // and the terrain as the CPU path resolves it; the first step one
    // substep; the source's depth while playing, none after a stop.
    NewContext();
    mvfxWaterDef water = Lake();
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, NULL, 0, NULL, 0, origin, 1.0f);
    const mvfxCompiledWater* compiled = &Live(s_planned)->effect->waters[0];
    mvfxPlannedCollider terrain = {0};
    {
        mvfxResolvedCollider resolved = {0};
        mvfxGridId grid = {0};
        CHECK(mvfxResolveWaterTerrain(Live(s_cpu), 0, &resolved, &grid), "a terrain");
        terrain = mvfxPlannedColliderOf(&resolved, grid);
    }
    int32_t wrong = 0;
    int32_t split = 0;
    for (int32_t s = 0; s < 40; ++s)
    {
        if (s == 30)
        {
            CHECK(mvfxInstance_Stop(s_context, s_cpu, mvfx_stopEmitting) == mvfx_success &&
                      mvfxInstance_Stop(s_context, s_planned, mvfx_stopEmitting) == mvfx_success,
                  "stopped");
        }
        const mvfxInstance* cpu = Live(s_cpu);
        float speed = mvfxShallowFastest(compiled, &cpu->waters[0]);
        CHECK(s == 0 || mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, cpu->clock, speed) ==
                            mvfx_success,
              "reported");
        bool playing = cpu->clock < cpu->stopTick;
        CHECK(mvfxContext_StepTicks(s_context, 4 * STEP_TICKS) == mvfx_success, "stepped");
        static mvfxWaterPlan plan;
        CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
        float kept = Live(s_cpu)->waters[0].stepSpeed;
        float dt = 0.0f;
        bool slowed = false;
        int32_t count = mvfxShallowSubsteps(
            compiled, kept, (double)plan.ticks / MVFX_TICKS_PER_SECOND, &dt, &slowed);
        float depth = playing ? (float)(0.25 * (double)plan.ticks / MVFX_TICKS_PER_SECOND) : 0.0f;
        wrong += Live(s_planned)->waters[0].stepSpeed != kept || plan.substeps != count ||
                 plan.dt != dt || plan.slowed != slowed || plan.sourceDepths[0] != depth ||
                 (s == 0 && plan.substeps != 1);
        wrong += !plan.hasTerrain ||
                 memcmp(plan.terrain.center, terrain.center, sizeof terrain.center) != 0 ||
                 memcmp(plan.terrain.halfExtents, terrain.halfExtents,
                        sizeof terrain.halfExtents) != 0 ||
                 plan.terrain.grid.index1 != terrain.grid.index1;
        split += count > 1;
    }
    CHECK(wrong == 0, "each step's plan as the CPU path's step");
    CHECK(split > 0, "some step split");
    mvfxDestroyContext(s_context);
}

static void TestUnreported(void)
{
    // A planned lake told nothing: its first step one substep (no water
    // before its fill), each later the most, not slowed; a restart counts
    // from no water again, its report forgotten.
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
    CHECK(mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, Live(s_planned)->clock, 1.0f) ==
                  mvfx_success &&
              mvfxInstance_Restart(s_context, s_planned) == mvfx_success &&
              mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success &&
              plan.substeps == 1 && plan.cleared,
          "a restart counts from no water");
    CHECK(Live(s_planned)->waters[0].reportedTick == -1, "its report forgotten");
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
    emitter.liquid.enabled = true;
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
    emitter.water = (mvfxWaterCoupling){.enabled = true, .water = 0, .radius = 0.05f};
    const double origin[3] = {0.0, 0.0, 0.0};
    Open(&water, 1, &emitter, 1, NULL, 0, origin, 1.0f);
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_Stop(s_context, s_planned, mvfx_stopEmitting) == mvfx_success &&
              mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success,
          "stopped");
    int64_t clock = Live(s_planned)->clock;
    CHECK(mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock, false) == mvfx_success &&
              !mvfxInstance_IsFinished(s_context, s_planned),
          "still ignored while its particles live");
    for (int32_t s = 0; s < 40; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success, "stepped");
    }
    clock = Live(s_planned)->clock;
    CHECK(!mvfxInstance_IsFinished(s_context, s_planned) &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock, true) == mvfx_success &&
              !mvfxInstance_IsFinished(s_context, s_planned) &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock, false) == mvfx_success &&
              mvfxInstance_IsFinished(s_context, s_planned),
          "still after the last death, finished");
    uint64_t misuse = mvfxGetContextMisuse(s_context);
    CHECK(mvfxInstance_SetWaterHolds(NULL, s_planned, 0, 0, true) == mvfx_errorInvalid &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 1, 0, true) == mvfx_errorInvalid &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, clock + 1, true) ==
                  mvfx_errorInvalid &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, -1, true) == mvfx_errorInvalid &&
              mvfxGetContextMisuse(s_context) == misuse + 3 &&
              mvfxInstance_SetWaterHolds(s_context, s_cpu, 0, 0, true) == mvfx_errorState &&
              mvfxInstance_SetWaterHolds(s_context, (mvfxInstanceId){0}, 0, 0, true) ==
                  mvfx_errorStale,
          "the holds setter's refusals");
    mvfxDestroyContext(s_context);
}

static void TestCoupling(void)
{
    // A world emitter coupled to a shallow surface (a pour's) of a moved,
    // turned and scaled instance: its plan's map is its resolved map (places mapped alike,
    // within the origin's rounding), its up, radius and strength the
    // resolved ones, the coupling's numbers its def's (the push in the
    // surface's lengths); an emitter without a coupling plans none.
    NewContext();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 8;
    water.size[1] = 8;
    water.center[1] = 0.3f;
    water.model = mvfx_waterShallow;
    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    emitters[0].space = mvfx_spaceWorld;
    emitters[0].water = (mvfxWaterCoupling){.enabled = true,
                                            .water = 0,
                                            .radius = 0.05f,
                                            .push = 0.04f,
                                            .pour = true,
                                            .buoyancy = 2.0f,
                                            .drag = 1.5f};
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
    const mvfxResolvedWater* resolved = &Live(s_planned)->emitters[0].water;
    const mvfxPlannedWaterCoupling* c = &plan.water;
    static const float places[3][3] = {{0, 0, 0}, {0.4f, -0.1f, 0.25f}, {-1.2f, 0.3f, 0.9f}};
    float apart = 0.0f;
    for (int32_t p = 0; p < 3; ++p)
    {
        float expected[3];
        float scaled[3];
        float turned[3];
        mvfxMapPoint(&resolved->map, places[p], expected);
        for (int32_t k = 0; k < 3; ++k)
        {
            scaled[k] = c->sigma * places[p][k];
        }
        mvfxRotate(c->rotation, scaled, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            apart = fmaxf(apart, fabsf(c->origin[k] + turned[k] - expected[k]));
        }
    }
    CHECK(c->water == 0 && apart < 1e-5f && c->sigma == resolved->map.sigma &&
              memcmp(c->up, resolved->up, sizeof c->up) == 0 && c->radius == resolved->radius &&
              c->strength == resolved->strength && c->radius == 0.05f / c->sigma &&
              c->drag == 1.5f && c->buoyancy == 2.0f && c->push == 0.04f / c->sigma && c->pour &&
              none.water.water == -1,
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
              view.def.sources[0].rate == 0.25f && view.def.hasTerrain &&
              view.def.terrain.grid.index1 == waters[0].terrain.grid.index1,
          "a lake's view");
    CHECK(mvfxEffect_GetWaterView(s_context, effect, 1, &view) == mvfx_success &&
              view.cells == mvfxDefaultWaterDef().size[0] * mvfxDefaultWaterDef().size[1] &&
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
                   info.facesHeld == 7 && info.volumePoured == 0.75 &&
                   Live(s_planned)->waters[0].reach == 0.2f;
    CHECK(counted, "each tick's report counted once, the latest reach");
    CHECK(mvfxInstance_Stop(s_context, s_planned, mvfx_stopAndClear) == mvfx_success &&
              mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 2 * STEP_TICKS, 1.0f, 5, 1.0) ==
                  mvfx_success &&
              mvfxInstance_GetWaterInfo(s_context, s_planned, 0, &info) == mvfx_success &&
              info.facesHeld == 0 && info.volumePoured == 0.0 &&
              Live(s_planned)->waters[0].reach == 0.0f,
          "a clear empties them, the reports before it ignored");
    uint64_t misuse = mvfxGetContextMisuse(s_context);
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
        mvfxGetContextMisuse(s_context) == misuse + 7 &&
        mvfxInstance_SetWaterCounts(s_context, s_cpu, 0, 0, 0.0f, 0, 0.0) == mvfx_errorState &&
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
    uint64_t misuse = mvfxGetContextMisuse(s_context);
    bool refused =
        mvfxInstance_GetWaterPlan(NULL, s_planned, 0, &plan) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterPlan(s_context, s_planned, 0, NULL) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterPlan(s_context, s_planned, 2, &plan) == mvfx_errorInvalid &&
        mvfxInstance_GetWaterPlan(s_context, s_cpu, 0, &plan) == mvfx_errorState &&
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
        mvfxGetContextMisuse(s_context) == misuse + 6 &&
        mvfxInstance_SetWaterSpeed(s_context, s_cpu, 0, 0, 1.0f) == mvfx_errorState &&
        mvfxInstance_SetWaterSpeed(s_context, (mvfxInstanceId){0}, 0, 0, 1.0f) == mvfx_errorStale;
    CHECK(refused, "the plans' and the speed setter's refusals");
    // An older report than the latest is ignored.
    CHECK(mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, STEP_TICKS, 3.0f) == mvfx_success &&
              mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, 0, 5.0f) == mvfx_success &&
              Live(s_planned)->waters[0].reportedSpeed == 3.0f,
          "the latest report counts");
    // A planned surface's fields are its simulation's.
    const mvfxWaterRead read = {
        .field = mvfx_waterHeight, .format = mvfx_formatFloat32, .size = {1, 1}};
    float out = 0.0f;
    const double point[3] = {0.0, 0.0, 0.0};
    mvfxWaterSample sample;
    CHECK(mvfxInstance_ReadWater(s_context, s_planned, 0, &read, &out, sizeof out) ==
                  mvfx_errorState &&
              mvfxInstance_SampleWater(s_context, s_planned, 0, point, 1, &sample) ==
                  mvfx_errorState &&
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
    TestUnreported();
    TestSplit();
    TestHolds();
    TestCoupling();
    TestView();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
