// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned smoke gases: a gas plan's span, gravity, forces and obstacles
// are the CPU path's step's, under a moving, turning, growing instance; a
// throttled plan's obstacles before are the last plan's; a planned gas
// holds until its simulation reports it empty past the stop and the
// splatting emitters' deaths; its fields are not read on the CPU path;
// liquid gases stay unplannable; a gas's view.

#include "collide.h"
#include "force.h"
#include "gas.h"
#include "gas_api.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

// Plans are held in static storage: the tests inline into main, whose
// frame the web build's 64 KiB stack must hold beside the library's.

#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)

static const mvfxBurst s_burst = {
    .time = 0.0f, .minCount = 8, .maxCount = 8, .cycles = 1, .probability = 1.0f};

// An emitter of a burst living a third of a second, splatting into gas 0
// when asked.
static mvfxEmitterDef Emitter(bool splats)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 16;
    emitter.rate = 0.0f;
    emitter.bursts = &s_burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.3f, 0.3f);
    emitter.gasSplat = (mvfxGasSplat){.enabled = splats, .gas = 0, .density = 1.0f};
    return emitter;
}

// A smoke gas of 8 by 8 by 4 cells in the instance's frame.
static mvfxGasDef Smoke(void)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 8;
    gas.size[1] = 8;
    gas.size[2] = 4;
    gas.cellSize = 0.25f;
    gas.center[1] = 1.0f;
    return gas;
}

static mvfxEffectId Effect(mvfxContext* context, const mvfxEmitterDef* emitter,
                           const mvfxGasDef* gas)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    return effect;
}

static mvfxInstanceId Instance(mvfxContext* context, mvfxEffectId effect, bool planned)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.seed = 7;
    def.planned = planned;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "instance");
    return instance;
}

static bool SameCollider(const mvfxPlannedCollider* p, const mvfxResolvedCollider* r)
{
    bool same = p->kind == r->kind && p->inside == r->inside && p->radius == r->radius &&
                p->scale == r->scale && p->grow == r->grow;
    for (int32_t k = 0; k < 3; ++k)
    {
        same = same && p->center[k] == r->center[k] && p->normal[k] == r->normal[k] &&
               p->halfExtents[k] == r->halfExtents[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        same = same && p->rotation[k] == r->rotation[k];
    }
    return same;
}

static bool SamePlanned(const mvfxPlannedCollider* a, const mvfxPlannedCollider* b)
{
    bool same = a->kind == b->kind && a->inside == b->inside && a->radius == b->radius &&
                a->scale == b->scale && a->grow == b->grow && a->grid.index1 == b->grid.index1;
    for (int32_t k = 0; k < 3; ++k)
    {
        same = same && a->center[k] == b->center[k] && a->normal[k] == b->normal[k] &&
               a->halfExtents[k] == b->halfExtents[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        same = same && a->rotation[k] == b->rotation[k];
    }
    return same;
}

// Moves an instance for step s: along, turning and growing.
static void Move(mvfxContext* context, mvfxInstanceId instance, int32_t s)
{
    const double origin[3] = {500.0 + 2.0 * s, 0.25 * s, 1.0};
    const float rotation[4] = {sinf(0.04f * (float)s), 0.0f, 0.0f, cosf(0.04f * (float)s)};
    CHECK(mvfxInstance_SetTransform(context, instance, origin, rotation, 1.0f + 0.1f * (float)s,
                                    false) == mvfx_success,
          "moved");
}

// Whether a gas plan is what the CPU path's gas step took: its span, its
// gravity, its forces and obstacles as the CPU instance resolved them.
static bool SameAsStepped(const mvfxGasPlan* plan, const mvfxInstance* cpu, mvfxGridId grid)
{
    const mvfxGasState* state = &cpu->gases[0];
    float gravity[3];
    mvfxGasGravity(cpu, 0, gravity);
    bool same = plan->start == cpu->stepStart && plan->ticks == cpu->clock - cpu->stepStart &&
                plan->stopTick == cpu->stopTick && plan->forceCount == 2;
    for (int32_t k = 0; k < 3; ++k)
    {
        same = same && plan->gravity[k] == gravity[k];
    }
    double time = (double)plan->start / (double)MVFX_TICKS_PER_SECOND;
    for (int32_t f = 0; same && f < plan->forceCount; ++f)
    {
        mvfxPlannedForce expected = mvfxPlannedForceOf(&cpu->gasForces[f], time);
        same = memcmp(&expected, &plan->forces[f], sizeof expected) == 0;
    }
    int32_t now = state->obstaclesNow;
    same = same && plan->obstacleCount == state->obstacleCount[now] &&
           plan->ownObstacleCount == 2 && plan->obstaclesKnown == state->obstaclesKnown;
    for (int32_t c = 0; same && c < plan->obstacleCount; ++c)
    {
        same = SameCollider(&plan->obstacles[c], &state->obstacles[now][c]);
    }
    for (int32_t c = 0; same && plan->obstaclesKnown && c < state->obstacleCount[1 - now]; ++c)
    {
        same = SameCollider(&plan->obstaclesBefore[c], &state->obstacles[1 - now][c]);
    }
    // The grids: the gas's heightfield's; none for a primitive.
    return same && plan->obstacles[1].grid.index1 == grid.index1 &&
           plan->obstacles[0].grid.index1 == 0;
}

static void TestResolved(void)
{
    // A world-space constant and a local turbulence, a sphere of its own
    // and a heightfield, and the instance's planes, under a moving,
    // turning, growing instance with a world gravity.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const float heights[4] = {0, 0.1f, 0.2f, 0.3f};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[2] = 2;
    gridDef.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &gridDef, &grid) == mvfx_success, "a grid");
    mvfxGasDef gas = Smoke();
    const mvfxForce forces[2] = {
        {.kind = mvfx_forceConstant, .space = mvfx_spaceWorld, .vector = {1, 0, 0}},
        {.kind = mvfx_forceTurbulence,
         .space = mvfx_spaceLocal,
         .strength = 2.0f,
         .noise = {.frequency = 0.8f,
                   .octaves = 3,
                   .lacunarity = 2.0f,
                   .gain = 0.5f,
                   .scroll = {0.3f, -0.2f, 0.1f},
                   .seed = 5}}};
    gas.forces = forces;
    gas.forceCount = 2;
    const mvfxCollider obstacles[2] = {{.kind = mvfx_colliderSphere,
                                        .space = mvfx_spaceLocal,
                                        .center = {0, 1, 0},
                                        .radius = 0.4f},
                                       {.kind = mvfx_colliderHeightfield,
                                        .center = {500, 0, 1},
                                        .halfExtents = {2, 0.5f, 2},
                                        .rotation = {0, 0, 0, 1},
                                        .grid = grid}};
    gas.obstacles = obstacles;
    gas.obstacleCount = 2;
    gas.hostObstacles = true;
    gas.gravitySpace = mvfx_gasWorld;
    gas.gravity[1] = -9.8f;
    mvfxEmitterDef emitter = Emitter(false);
    mvfxEffectId effect = Effect(context, &emitter, &gas);
    mvfxInstanceId cpu = Instance(context, effect, false);
    mvfxInstanceId planned = Instance(context, effect, true);
    const mvfxCollider hosts[2] = {
        {.kind = mvfx_colliderPlane, .center = {0, 0.5f, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderPlane,
         .space = mvfx_spaceLocal,
         .center = {0, 0, -2},
         .normal = {0, 0, 1}}};
    bool same = true;
    bool known = false;
    static mvfxGasPlan plan;
    for (int32_t s = 0; s < 10 && same; ++s)
    {
        mvfxInstanceId both[2] = {cpu, planned};
        for (int32_t i = 0; i < 2; ++i)
        {
            Move(context, both[i], s);
            CHECK(mvfxInstance_SetColliders(context, both[i], hosts, s < 5 ? 2 : 1) == mvfx_success,
                  "colliders");
        }
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
        CHECK(mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success, "a plan");
        same = SameAsStepped(&plan, mvfxContextInstance(context, cpu), grid) &&
               plan.obstacleCount == (s < 5 ? 4 : 3) && plan.cleared == (s == 0);
        known = known || plan.obstaclesKnown;
    }
    CHECK(same && known, "the gas plan is the CPU path's step's");
    // A teleport: the obstacles before are not known.
    const double far[3] = {-100, 0, 0};
    const float still[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(context, planned, far, still, 1.0f, true) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success &&
              !plan.obstaclesKnown,
          "a teleport's obstacles before unknown");
    mvfxDestroyContext(context);
}

static void TestThrottled(void)
{
    // Unseen, stepping every third step of 0.4 seconds: a plan gathers
    // 1.2 seconds, two of the instance's steps (the longest is a second),
    // and has as its obstacles before the last plan's.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Smoke();
    const mvfxForce forces[2] = {
        {.kind = mvfx_forceConstant, .space = mvfx_spaceWorld, .vector = {1, 0, 0}},
        {.kind = mvfx_forceConstant, .space = mvfx_spaceLocal, .vector = {0, 0, 1}}};
    gas.forces = forces;
    gas.forceCount = 2;
    const mvfxCollider obstacles[1] = {
        {.kind = mvfx_colliderSphere, .center = {500, 1, 1}, .radius = 0.4f}};
    gas.obstacles = obstacles;
    gas.obstacleCount = 1;
    mvfxEmitterDef emitter = Emitter(false);
    mvfxEffectId effect = Effect(context, &emitter, &gas);
    mvfxInstanceId planned = Instance(context, effect, true);
    CHECK(mvfxInstance_SetVisible(context, planned, false) == mvfx_success &&
              mvfxInstance_SetOffscreen(context, planned, mvfx_offscreenThrottle, 3) ==
                  mvfx_success,
          "throttled");
    static mvfxGasPlan plan;
    static mvfxPlannedCollider last;
    bool same = true;
    int32_t gathered = 0;
    int32_t compared = 0;
    const int64_t ticks = 2 * MVFX_TICKS_PER_SECOND / 5;
    for (int32_t s = 0; s < 12 && same; ++s)
    {
        Move(context, planned, s);
        CHECK(mvfxContext_StepTicks(context, ticks) == mvfx_success &&
                  mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success,
              "stepped");
        if (plan.ticks == 0)
        {
            continue;
        }
        gathered += plan.ticks > MVFX_TICKS_PER_SECOND ? 1 : 0;
        if (plan.obstaclesKnown)
        {
            same = SamePlanned(&plan.obstaclesBefore[0], &last);
            compared += 1;
        }
        last = plan.obstacles[0];
    }
    CHECK(same && gathered > 0 && compared > 1, "a gathered plan's obstacles before");
    mvfxDestroyContext(context);
}

// Whether every refusal of the setter holds.
static bool SetterRefuses(mvfxContext* context, mvfxInstanceId planned, mvfxInstanceId cpu,
                          int64_t clock)
{
    uint64_t misuse = mvfxGetContextMisuse(context);
    bool refused =
        mvfxInstance_SetGasHolds(NULL, planned, 0, 0, false) == mvfx_errorInvalid &&
        mvfxInstance_SetGasHolds(context, planned, 1, 0, false) == mvfx_errorInvalid &&
        mvfxInstance_SetGasHolds(context, planned, 0, -1, false) == mvfx_errorInvalid &&
        mvfxInstance_SetGasHolds(context, planned, 0, clock + 1, false) == mvfx_errorInvalid &&
        mvfxGetContextMisuse(context) == misuse + 3 &&
        mvfxInstance_SetGasHolds(context, cpu, 0, 0, false) == mvfx_errorState &&
        mvfxInstance_SetGasHolds(context, (mvfxInstanceId){0}, 0, 0, false) == mvfx_errorStale;
    static mvfxGasPlan plan;
    return refused && mvfxInstance_GetGasPlan(context, cpu, 0, &plan) == mvfx_errorState &&
           mvfxInstance_GetGasPlan(context, planned, 1, &plan) == mvfx_errorInvalid &&
           mvfxInstance_GetGasPlan(NULL, planned, 0, &plan) == mvfx_errorInvalid &&
           mvfxInstance_GetGasPlan(context, planned, 0, NULL) == mvfx_errorInvalid;
}

static void TestHolds(void)
{
    // Particles splatting into the gas live 0.3 seconds from a burst at 0;
    // the instance stops at step 6 (0.1 s), its particles die at 0.3 s.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Smoke();
    mvfxEmitterDef emitter = Emitter(true);
    mvfxEffectId effect = Effect(context, &emitter, &gas);
    mvfxInstanceId planned = Instance(context, effect, true);
    mvfxInstanceId cpu = Instance(context, effect, false);
    for (int32_t s = 0; s < 6; ++s)
    {
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
    }
    CHECK(mvfxInstance_Stop(context, planned, mvfx_stopEmitting) == mvfx_success, "stopped");
    int64_t stop = 6 * STEP_TICKS;
    // The last death, within a step of 0.3 seconds.
    int64_t death = (int64_t)ceil(0.3 * (double)MVFX_TICKS_PER_SECOND);
    for (int32_t s = 6; s < 30; ++s)
    {
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
    }
    int64_t clock = 30 * STEP_TICKS;
    // Held until told: empty before the stop, or before the deaths, does
    // not count; past both it does; that it holds always counts.
    bool held = !mvfxInstance_IsFinished(context, planned);
    bool before =
        mvfxInstance_SetGasHolds(context, planned, 0, stop - 1, false) == mvfx_success &&
        !mvfxInstance_IsFinished(context, planned) &&
        mvfxInstance_SetGasHolds(context, planned, 0, death - STEP_TICKS, false) == mvfx_success &&
        !mvfxInstance_IsFinished(context, planned);
    bool after =
        mvfxInstance_SetGasHolds(context, planned, 0, death + STEP_TICKS, false) == mvfx_success &&
        mvfxInstance_IsFinished(context, planned);
    bool again = mvfxInstance_SetGasHolds(context, planned, 0, clock, true) == mvfx_success &&
                 !mvfxInstance_IsFinished(context, planned);
    CHECK(held && before && after && again, "a planned gas holds until reported empty");
    CHECK(SetterRefuses(context, planned, cpu, clock), "the gas plan's refusals");
    // Its fields are the simulation's.
    static float cells[8 * 8 * 4];
    const double point[3] = {0, 1, 0};
    float sampled = 0.0f;
    const mvfxGasRead read = {.field = mvfx_gasDensity,
                              .format = mvfx_formatFloat32,
                              .size = {8, 8, 4},
                              .low = 0.0f,
                              .high = 1.0f};
    CHECK(mvfxInstance_ReadGas(context, planned, 0, mvfx_gasDensity, cells, 8 * 8 * 4) ==
                  mvfx_errorState &&
              mvfxInstance_SampleGas(context, planned, 0, mvfx_gasDensity, point, 1, &sampled) ==
                  mvfx_errorState &&
              mvfxInstance_ReadGasAs(context, planned, 0, &read, cells, sizeof cells) ==
                  mvfx_errorState,
          "a planned gas's fields not read");
    // A clear empties the gas: no report needed.
    mvfxInstanceId cleared = Instance(context, effect, true);
    CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_Stop(context, cleared, mvfx_stopAndClear) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_IsFinished(context, cleared),
          "a cleared planned gas holds nothing");
    mvfxDestroyContext(context);
}

static void TestLiquidRefused(void)
{
    // A liquid gas still keeps its effect unplannable.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Smoke();
    gas.kind = mvfx_gasLiquid;
    gas.size[2] = 1;
    mvfxEmitterDef emitter = Emitter(false);
    emitter.burstCount = 0;
    emitter.hostSpawnCapacity = 16;
    emitter.liquid.enabled = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = 0.1f;
    mvfxEffectId effect = Effect(context, &emitter, &gas);
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.planned = true;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_errorState && instance.index1 == 0,
          "a liquid gas unplannable");
    mvfxDestroyContext(context);
}

static void TestView(void)
{
    // A gas of 64 by 20 by 6 cells with a source and a force: its view
    // has its def, its copies and its levels (an axis halved, rounding
    // up, while every axis of more than one cell has at least 4: 6 halves
    // to 3, the last).
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Smoke();
    gas.size[0] = 64;
    gas.size[1] = 20;
    gas.size[2] = 6;
    const mvfxGasSource source = {.shape = mvfx_gasSphere, .radius = 0.5f, .density = 2.0f};
    gas.sources = &source;
    gas.sourceCount = 1;
    const mvfxForce force = {.kind = mvfx_forceConstant, .vector = {0, 1, 0}};
    gas.forces = &force;
    gas.forceCount = 1;
    mvfxEmitterDef emitter = Emitter(false);
    mvfxEffectId effect = Effect(context, &emitter, &gas);
    static mvfxGasView view;
    CHECK(mvfxEffect_GetGasView(context, effect, 0, &view) == mvfx_success, "a view");
    static const int32_t levels[2][3] = {{64, 20, 6}, {32, 10, 3}};
    bool same = view.levelCount == 2 && view.cells == 64 * 20 * 6 && view.faces[0] == 65 * 20 * 6 &&
                view.faces[1] == 64 * 21 * 6 && view.faces[2] == 64 * 20 * 7 && view.emitter == -1;
    for (int32_t l = 0; same && l < 2; ++l)
    {
        same = memcmp(view.levels[l], levels[l], sizeof levels[l]) == 0;
    }
    same = same && view.def.sourceCount == 1 && view.def.sources != &source &&
           view.def.sources[0].density == 2.0f && view.def.forceCount == 1 &&
           view.def.forces != &force && view.def.forces[0].vector[1] == 1.0f &&
           view.def.obstacles == NULL && view.def.cellSize == gas.cellSize;
    CHECK(same, "a gas's view");
    CHECK(mvfxEffect_GetGasView(context, effect, 1, &view) == mvfx_errorInvalid &&
              mvfxEffect_GetGasView(NULL, effect, 0, &view) == mvfx_errorInvalid &&
              mvfxEffect_GetGasView(context, effect, 0, NULL) == mvfx_errorInvalid &&
              mvfxEffect_GetGasView(context, (mvfxEffectId){0}, 0, &view) == mvfx_errorStale,
          "a gas view's refusals");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestResolved();
    TestThrottled();
    TestHolds();
    TestLiquidRefused();
    TestView();
    return s_failures == 0 ? 0 : 1;
}
