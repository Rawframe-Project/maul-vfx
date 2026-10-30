// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned smoke gases: a gas plan's span, gravity, forces and obstacles
// are their defs resolved as plan.h documents (mvfxGasPlan), in the box's
// frame mvfxInstance_GetGasInfo places, under a moving, turning, growing
// instance (the oracles in plan_oracle.h); a throttled plan's obstacles
// before are the last plan's; a planned gas holds until its simulation
// reports it empty past the stop and the splatting emitters' deaths; the
// emitters pouring into it map into the box as mvfxPlannedPour says; its
// fields are not read on the CPU path; a gas's view.

#include "plan_oracle.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
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
    emitter.gasSplat = (mvfxGasSplat){.enable = splats, .gas = 0, .density = 1.0f};
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
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
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

// The obstacles a plan says moved and did not, over a run.
static int32_t s_moved;
static int32_t s_unmoved;

// Whether each obstacle reads as moved just when the one before of its
// index is known and has its kind, side, sizes and grid.
static bool MovedAsBefore(const mvfxGasPlan* plan, int32_t before)
{
    bool all = true;
    for (int32_t c = 0; c < plan->obstacleCount; ++c)
    {
        const mvfxPlannedCollider* a = &plan->obstacles[c];
        const mvfxPlannedCollider* b = &plan->obstaclesBefore[c];
        bool same = plan->obstaclesKnown && c < before && a->kind == b->kind &&
                    a->inside == b->inside && a->radius == b->radius &&
                    a->grid.index1 == b->grid.index1 && a->grid.generation == b->grid.generation;
        for (int32_t k = 0; k < 3; ++k)
        {
            same = same && a->halfExtents[k] == b->halfExtents[k];
        }
        all = all && plan->obstacleMoved[c] == same;
        s_moved += same ? 1 : 0;
        s_unmoved += same ? 0 : 1;
    }
    return all;
}

// The instance's place at the end of a planned instance's last step
// (its emitters' plans' to), the box its gas's info places then, and the
// emitters' plan.
static mvfxEmitterPlan s_emitter;
static mvfxGasInfo s_box;

static void ReadPlace(mvfxContext* context, mvfxInstanceId planned)
{
    CHECK(mvfxInstance_GetEmitterPlan(context, planned, 0, &s_emitter) == mvfx_success &&
              mvfxInstance_GetGasInfo(context, planned, 0, &s_box) == mvfx_success,
          "the place and the box");
}

// Whether a gas plan is its gas's step as plan.h documents it
// (mvfxGasPlan), in the box's frame at the step's end: the emitters'
// span, no stop; the world's gravity turned into the box, over its scale;
// each force its def through the frame of its space, its noise at the
// plan's start; each obstacle, its own then the instance's, its def
// through its space's frame grown by no radius; the obstacles before the
// last plan's, when it is known.
static bool SameAsResolved(const mvfxGasPlan* plan, const mvfxGasDef* gas,
                           const mvfxCollider* hosts, int32_t hostCount, const mvfxGasPlan* last)
{
    Frame world = BoxFrame(&s_box, &s_emitter.to, mvfx_spaceWorld, NULL);
    double gravity[3];
    FrameVector(&world, gas->gravity, world.sigma, gravity);
    bool same = plan->start == s_emitter.start && plan->ticks == s_emitter.ticks &&
                plan->stopTick > plan->start + plan->ticks && Near3(plan->gravity, gravity) &&
                plan->forceCount == gas->forceCount;
    double time = (double)plan->start / (double)MVFX_TICKS_PER_SECOND;
    for (int32_t f = 0; same && f < plan->forceCount; ++f)
    {
        const mvfxForce* def = &gas->forces[f];
        Frame frame = BoxFrame(&s_box, &s_emitter.to, def->space, NULL);
        same =
            SameForce(&plan->forces[f], def, &frame, 0.0f) &&
            (def->kind != mvfx_forceTurbulence || SameLattice(&plan->forces[f], def, &frame, time));
    }
    same = same && plan->obstacleCount == gas->obstacleCount + hostCount &&
           plan->ownObstacleCount == gas->obstacleCount;
    for (int32_t c = 0; same && c < plan->obstacleCount; ++c)
    {
        const mvfxCollider* def =
            c < gas->obstacleCount ? &gas->obstacles[c] : &hosts[c - gas->obstacleCount];
        Frame frame = BoxFrame(&s_box, &s_emitter.to, def->space, NULL);
        same = SameCollider(&plan->obstacles[c], def, def->kind == mvfx_colliderHeightfield, &frame,
                            0.0);
    }
    int32_t before = last != NULL ? last->obstacleCount : 0;
    same = same && plan->obstaclesKnown == (last != NULL) && MovedAsBefore(plan, before);
    for (int32_t c = 0; same && c < before; ++c)
    {
        same = SamePlanned(&plan->obstaclesBefore[c], &last->obstacles[c]);
    }
    return same;
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
         .strength = {.kind = mvfx_sourceRange, .range = {2.0f, 2.0f}},
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
    mvfxInstanceId planned = Instance(context, effect, true);
    const mvfxCollider hosts[2] = {
        {.kind = mvfx_colliderPlane, .center = {0, 0.5f, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderPlane,
         .space = mvfx_spaceLocal,
         .center = {0, 0, -2},
         .normal = {0, 0, 1}}};
    bool same = true;
    static mvfxGasPlan plan;
    static mvfxGasPlan last;
    for (int32_t s = 0; s < 10 && same; ++s)
    {
        Move(context, planned, s);
        int32_t hostCount = s < 5 ? 2 : 1;
        CHECK(mvfxInstance_SetColliders(context, planned, hosts, hostCount) == mvfx_success,
              "colliders");
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
        CHECK(mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success, "a plan");
        ReadPlace(context, planned);
        // The grids: the gas's heightfield's; none for a primitive.
        same = SameAsResolved(&plan, &gas, hosts, hostCount, s > 0 ? &last : NULL) &&
               plan.ticks == STEP_TICKS && plan.start == (int64_t)s * STEP_TICKS &&
               plan.cleared == (s == 0) && plan.obstacles[1].grid.index1 == grid.index1 &&
               plan.obstacles[0].grid.index1 == 0;
        last = plan;
    }
    CHECK(same && s_moved > 0 && s_unmoved > 0, "the gas plan is its defs resolved in the box");
    // A teleport: the obstacles before are not known.
    const double far[3] = {-100, 0, 0};
    const float still[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(context, planned, far, still, 1.0f, true) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success &&
              !plan.obstaclesKnown && MovedAsBefore(&plan, 0),
          "a teleport's obstacles before unknown");
    mvfxDestroyContext(context);
}

// Whether a planned pour is its emitter's (plan.h, mvfxPlannedPour): the
// map of its particles, in its space, into the box's frame (a world
// emitter's offsets from its anchor after the step); its splat's rates
// and drag.
static bool SamePlannedPour(const mvfxPlannedPour* pour, const mvfxEmitterDef* emitter, int32_t e)
{
    Frame frame = BoxFrame(&s_box, &s_emitter.to, emitter->space, s_emitter.anchorAfter);
    const mvfxGasSplat* splat = &emitter->gasSplat;
    return pour->emitter == e && SamePour(pour, &frame) && pour->drag == splat->drag &&
           pour->rates[0] == splat->density && pour->rates[1] == splat->temperature &&
           pour->rates[2] == splat->fuel;
}

static void TestPours(void)
{
    // A world emitter and a local one splatting, and one not, under a
    // moving, turning, growing instance.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitters[3] = {Emitter(true), Emitter(false), Emitter(true)};
    emitters[0].space = mvfx_spaceWorld;
    emitters[2].space = mvfx_spaceLocal;
    emitters[2].gasSplat = (mvfxGasSplat){
        .enable = true, .density = 0.5f, .temperature = 2.0f, .fuel = 0.25f, .drag = 3.0f};
    mvfxGasDef gas = Smoke();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 3;
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    static mvfxEmitterView views[2];
    CHECK(mvfxEffect_GetEmitterView(context, effect, 0, &views[0]) == mvfx_success &&
              mvfxEffect_GetEmitterView(context, effect, 1, &views[1]) == mvfx_success &&
              views[0].splatGas == 0 && views[1].splatGas == -1,
          "the views name the gas poured into");
    mvfxInstanceId planned = Instance(context, effect, true);
    static mvfxGasPlan plan;
    bool same = true;
    bool anchored = false;
    for (int32_t s = 0; s < 8 && same; ++s)
    {
        // Past MVFX_REBASE_DISTANCE: the world emitter's anchor moves.
        const double origin[3] = {2000.0 + 2.0 * s, 0.25 * s, 1.0};
        const float rotation[4] = {sinf(0.04f * (float)s), 0.0f, 0.0f, cosf(0.04f * (float)s)};
        CHECK(mvfxInstance_SetTransform(context, planned, origin, rotation, 1.0f + 0.1f * (float)s,
                                        false) == mvfx_success,
              "moved");
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
        CHECK(mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success, "a plan");
        ReadPlace(context, planned);
        same = plan.pourCount == 2 && SamePlannedPour(&plan.pours[0], &emitters[0], 0) &&
               SamePlannedPour(&plan.pours[1], &emitters[2], 2);
        anchored = anchored || s_emitter.anchorAfter[0] > 1000.0;
    }
    CHECK(same && anchored, "the planned pours map into the box");
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
    uint64_t misuse = mvfxContext_GetMisuse(context);
    bool refused =
        mvfxInstance_SetGasHolds(NULL, planned, 0, 0, false) == mvfx_errorInvalid &&
        mvfxInstance_SetGasHolds(context, planned, 1, 0, false) == mvfx_errorInvalid &&
        mvfxInstance_SetGasHolds(context, planned, 0, -1, false) == mvfx_errorInvalid &&
        mvfxInstance_SetGasHolds(context, planned, 0, clock + 1, false) == mvfx_errorInvalid &&
        mvfxContext_GetMisuse(context) == misuse + 3 &&
        mvfxInstance_SetGasHolds(context, cpu, 0, 0, false) == mvfx_errorUnsupported &&
        mvfxInstance_SetGasHolds(context, (mvfxInstanceId){0}, 0, 0, false) == mvfx_errorStale;
    static mvfxGasPlan plan;
    return refused && mvfxInstance_GetGasPlan(context, cpu, 0, &plan) == mvfx_errorUnsupported &&
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
    static mvfxGasPlan plan;
    CHECK(mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success &&
              plan.stopTick == stop && plan.start + plan.ticks == clock,
          "the plan's stop tick the clock the instance stopped at");
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
                  mvfx_errorUnsupported &&
              mvfxInstance_SampleGas(context, planned, 0, mvfx_gasDensity, point, 1, &sampled) ==
                  mvfx_errorUnsupported &&
              mvfxInstance_ReadGasAs(context, planned, 0, &read, cells, sizeof cells) ==
                  mvfx_errorUnsupported,
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
    TestPours();
    TestThrottled();
    TestHolds();
    TestView();
    return s_failures == 0 ? 0 : 1;
}
