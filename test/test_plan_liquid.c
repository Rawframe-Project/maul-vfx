// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned liquid gases: a planned instance cuts its steps into substeps
// as gas.h counts them (mvfxGasDef.courant) from the fastest speed its
// simulation reports (the CPU twin's own, read from its records, when
// reported each step; gravity's alone with no particle alive; the most
// before any report), and hands over a plan a substep: the span, the
// move over the first alone, the births by their substep's start, the
// serials owed (the CPU twin's); the gas's plan of each substep spans it
// (its clear in the first alone, its obstacles before as the substep
// before left them, its forces at its start), the whole plans refused.
// The speed setter: the latest report counts, a restart forgets them, the
// refusals; births past a plan's room refused; a liquid gas's plan maps
// its particles into its box, as a surface gas's does. That the CPU path
// counts its own substeps from its own speed the same way is left to the
// GPU part's harness, which compares their results.

#include "particles.h"
#include "plan_oracle.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 30)

// A 2D liquid gas of 16 by 16 cells of 0.1 about the instance, closed.
static mvfxGasDef Liquid(void)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.closedWalls = 63;
    return gas;
}

// A grid liquid's emitter on gas 0, born at 120 a second at 1 to 2 a
// second upward, falling under gravity.
static mvfxEmitterDef Emitter(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 256;
    emitter.rate = 120.0f;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.velocity.speed = (mvfxRange){1.0f, 2.0f};
    emitter.motion.gravity[1] = -9.81f;
    emitter.liquid.enable = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = 0.05f;
    return emitter;
}

// An effect of the emitter and the gases, a liquid then a smoke.
static mvfxEffectId Effect(mvfxContext* context, const mvfxGasDef* gases, int32_t gasCount)
{
    mvfxEmitterDef emitter = Emitter();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.gases = gases;
    def.gasCount = gasCount;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    return effect;
}

static mvfxInstanceId Instance(mvfxContext* context, mvfxEffectId effect, bool planned)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.seed = 5;
    def.planned = planned;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "instance");
    return instance;
}

// The CPU twin's fastest particle speed now, from its records.
static float Fastest(mvfxContext* context, mvfxInstanceId cpu)
{
    static TestParticle records[256];
    int32_t count = ReadParticles(context, cpu, 0, records, 256);
    float fastest = 0.0f;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* v = records[i].velocity;
        fastest = fmaxf(fastest, v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    }
    return sqrtf(fastest);
}

// The substeps gas.h gives a step of ticks of a liquid gas of cell h and
// courant number courant, its emitter's gravity g and fastest speed v:
// ceil((v + sqrt(5 h |g|)) t / (h courant)), from 1 to
// MVFX_MAX_LIQUID_SUBSTEPS.
static int32_t LiquidSubsteps(float v, const mvfxGasDef* gas, double g, int64_t ticks)
{
    double h = (double)gas->cellSize;
    double t = (double)ticks / (double)MVFX_TICKS_PER_SECOND;
    double n = ceil(((double)v + sqrt(5.0 * h * g)) * t / (h * (double)gas->courant));
    return (int32_t)fmin(fmax(n, 1.0), (double)MVFX_MAX_LIQUID_SUBSTEPS);
}

// The instance's place at step s: along X, within the gas.
static void Move(mvfxContext* context, mvfxInstanceId instance, int32_t s)
{
    const double origin[3] = {0.002 * s, 0.0, 0.0};
    const float rotation[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(context, instance, origin, rotation, 1.0f, false) ==
              mvfx_success,
          "moved");
}

// What a run's plans broke: their steps' spans, the births' order and
// ticks, the serials owed, the move.
typedef struct Broken
{
    int32_t counts;
    int32_t spans;
    int32_t births;
    int32_t owed;
    int32_t moves;
    int32_t whole;
    // The context's steps the plans split.
    int32_t split;
} Broken;

// Checks a planned instance's plans of a context's step of count substeps
// from start against the CPU path's: the serials go on from *serial.
static void CheckSteps(mvfxContext* context, mvfxInstanceId planned, int64_t start, int32_t count,
                       const float velocity[3], uint64_t* serial, Broken* broken)
{
    static mvfxEmitterPlan plan;
    static mvfxGasPlan gasPlan;
    int64_t done = 0;
    for (int32_t k = 0; k < count; ++k)
    {
        int64_t to = STEP_TICKS * (k + 1) / count;
        CHECK(mvfxInstance_GetEmitterStepPlan(context, planned, 0, k, &plan) == mvfx_success,
              "a step's plan");
        broken->spans += plan.start != start + done || plan.ticks != to - done;
        // The gas's plan of the step spans it too.
        CHECK(mvfxInstance_GetGasStepPlan(context, planned, 0, k, &gasPlan) == mvfx_success,
              "a gas's step's plan");
        broken->spans += gasPlan.start != plan.start || gasPlan.ticks != plan.ticks;
        for (int32_t b = 0; b < plan.birthCount; ++b)
        {
            const mvfxPlannedBirth* birth = &plan.births[b];
            uint64_t at = (uint64_t)birth->serialLow | (uint64_t)birth->serialHigh << 32u;
            broken->births += at != *serial || (int64_t)birth->tick >= plan.ticks;
            *serial += 1;
        }
        broken->owed += plan.owed != *serial;
        bool moved = true;
        for (int32_t c = 0; c < 3; ++c)
        {
            float expected = k == 0 ? velocity[c] : 0.0f;
            moved = moved && plan.velocity[c] == expected &&
                    (k == 0 || plan.from.origin[c] == plan.to.origin[c]);
        }
        broken->moves += !moved;
        done = to;
    }
    // The whole query is refused while the plans hold more steps.
    mvfxResult whole = mvfxInstance_GetEmitterPlan(context, planned, 0, &plan);
    broken->whole += count > 1 ? whole != mvfx_errorState : whole != mvfx_success;
    whole = mvfxInstance_GetGasPlan(context, planned, 0, &gasPlan);
    broken->whole += count > 1 ? whole != mvfx_errorState : whole != mvfx_success;
    CHECK(mvfxInstance_GetEmitterStepPlan(context, planned, 0, count, &plan) == mvfx_errorInvalid &&
              mvfxInstance_GetEmitterStepPlan(context, planned, 1, 0, &plan) == mvfx_errorInvalid &&
              mvfxInstance_GetGasStepPlan(context, planned, 0, count, &gasPlan) ==
                  mvfx_errorInvalid &&
              mvfxInstance_GetGasStepPlan(context, planned, 0, -1, &gasPlan) == mvfx_errorInvalid &&
              mvfxInstance_GetGasStepPlan(context, planned, 99, 0, &gasPlan) == mvfx_errorInvalid &&
              mvfxInstance_GetGasStepPlan(context, planned, 0, 0, NULL) == mvfx_errorInvalid &&
              mvfxInstance_GetGasStepPlan(NULL, planned, 0, 0, &gasPlan) == mvfx_errorInvalid,
          "a step's plan's refusals");
}

// Twins of one seed in one context: the CPU path's and the planned, and
// their liquid gas.
typedef struct Twins
{
    mvfxContext* context;
    mvfxInstanceId cpu;
    mvfxInstanceId planned;
    mvfxGasDef gas;
} Twins;

// Steps the twins once (step s), the planned one told the CPU one's
// fastest speed first when reporting, and checks its plans.
static void StepTwins(const Twins* t, int32_t s, bool reporting, uint64_t* serial, Broken* broken)
{
    int64_t start = s * STEP_TICKS;
    float fastest = Fastest(t->context, t->cpu);
    mvfxResult reported =
        reporting ? mvfxInstance_SetLiquidSpeed(t->context, t->planned, 0, start, fastest)
                  : mvfx_success;
    Move(t->context, t->cpu, s);
    Move(t->context, t->planned, s);
    // Before any report with particles alive, the most; with none alive,
    // gravity's alone (Emitter's, 9.81 down).
    int32_t expected = s > 0 && !reporting ? MVFX_MAX_LIQUID_SUBSTEPS
                                           : LiquidSubsteps(fastest, &t->gas, 9.81, STEP_TICKS);
    // The CPU path's move over its first substep.
    int64_t first = STEP_TICKS / expected;
    double seconds = (double)first / (double)MVFX_TICKS_PER_SECOND;
    float velocity[3] = {s > 0 ? (float)(0.002 / seconds) : 0.0f, 0.0f, 0.0f};
    int32_t count = 0;
    CHECK(reported == mvfx_success &&
              mvfxContext_StepTicks(t->context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetPlanSteps(t->context, t->planned, &count) == mvfx_success,
          "stepped");
    broken->counts += count != expected;
    broken->split += count > 1;
    CheckSteps(t->context, t->planned, start, count, velocity, serial, broken);
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(t->context, t->cpu, 0, &stats) == mvfx_success, "stats");
    broken->owed += stats.owed != *serial;
}

static void TestSplit(void)
{
    // Twins of one seed, the planned one told the CPU one's fastest speed
    // at each step's start from its third step on: its first step has no
    // particle (gravity's count, the CPU path's), its second no report
    // (the most), the rest counted as gas.h counts them from the CPU twin's
    // fastest speed.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Twins t = {0};
    CHECK(mvfxCreateContext(&contextDef, &t.context) == mvfx_success, "context");
    t.gas = Liquid();
    mvfxEffectId effect = Effect(t.context, &t.gas, 1);
    t.cpu = Instance(t.context, effect, false);
    t.planned = Instance(t.context, effect, true);
    Broken broken = {0};
    uint64_t serial = 0;
    for (int32_t s = 0; s < 20; ++s)
    {
        StepTwins(&t, s, s >= 2, &serial, &broken);
    }
    CHECK(broken.counts == 0 && broken.split > 2, "a planned liquid's split the CPU path's");
    CHECK(broken.spans == 0, "each step's span");
    CHECK(broken.births == 0 && broken.owed == 0, "each step's births and serials");
    CHECK(broken.moves == 0, "the move over the first step alone");
    CHECK(broken.whole == 0, "the whole plan of one step alone");
    int32_t count = -1;
    CHECK(mvfxInstance_GetPlanSteps(NULL, t.planned, &count) == mvfx_errorInvalid && count == 0 &&
              mvfxInstance_GetPlanSteps(t.context, t.planned, NULL) == mvfx_errorInvalid &&
              mvfxInstance_GetPlanSteps(t.context, t.cpu, &count) == mvfx_errorUnsupported &&
              mvfxInstance_GetPlanSteps(t.context, (mvfxInstanceId){0}, &count) == mvfx_errorStale,
          "the steps' refusals");
    mvfxDestroyContext(t.context);
}

static void TestGasSteps(void)
{
    // A liquid gas of a courant number of 0.2 (every step split) with an
    // obstacle, beside a smoke gas stirred by a turbulence scrolling with
    // time: each gas's substep's plan clears in the context's first
    // step's first alone; the liquid's knows its obstacles before but in
    // that first substep, in a later substep where the one before left
    // them and followed (the CPU path resolves them again each substep);
    // the smoke's takes its forces at its own start.
    static const mvfxCollider obstacle = {.kind = mvfx_colliderSphere,
                                          .space = 1,
                                          .center = {0.3f, 0.0f, 0.0f},
                                          .radius = 0.2f,
                                          .rotation = {0, 0, 0, 1}};
    static const mvfxForce turbulence = {.kind = mvfx_forceTurbulence,
                                         .strength = 1.0f,
                                         .noise = {.frequency = 2.0f,
                                                   .octaves = 2,
                                                   .lacunarity = 2.0f,
                                                   .gain = 0.5f,
                                                   .scroll = {1.0f, 0.0f, -2.0f}}};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gases[2] = {Liquid(), mvfxDefaultGasDef()};
    gases[0].courant = 0.2f;
    gases[0].obstacles = &obstacle;
    gases[0].obstacleCount = 1;
    gases[1].size[0] = 4;
    gases[1].size[1] = 4;
    gases[1].forces = &turbulence;
    gases[1].forceCount = 1;
    mvfxEffectId effect = Effect(context, gases, 2);
    mvfxInstanceId planned = Instance(context, effect, true);
    mvfxGasInfo info;
    CHECK(mvfxInstance_GetGasInfo(context, planned, 1, &info) == mvfx_success, "the smoke's box");
    // The turbulence is the world's: the instance's place is not read.
    Frame box = BoxFrame(&info, NULL, mvfx_spaceWorld, NULL);
    int32_t split = 0;
    int32_t cleared = 0;
    int32_t obstacles = 0;
    int32_t forces = 0;
    for (int32_t s = 0; s < 8; ++s)
    {
        int32_t count = 0;
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
                  mvfxInstance_GetPlanSteps(context, planned, &count) == mvfx_success,
              "stepped");
        split += count > 1;
        for (int32_t k = 0; k < count; ++k)
        {
            static mvfxGasPlan plan;
            static mvfxGasPlan stirred;
            CHECK(mvfxInstance_GetGasStepPlan(context, planned, 0, k, &plan) == mvfx_success &&
                      mvfxInstance_GetGasStepPlan(context, planned, 1, k, &stirred) == mvfx_success,
                  "the gases' step's plans");
            cleared += plan.cleared != (s == 0 && k == 0) || stirred.cleared != plan.cleared;
            bool still = plan.obstaclesKnown && plan.obstacleCount == 1 &&
                         memcmp(plan.obstaclesBefore[0].center, plan.obstacles[0].center,
                                sizeof plan.obstacles[0].center) == 0;
            bool first = s == 0 && k == 0;
            obstacles += first ? plan.obstaclesKnown || plan.obstacleMoved[0]
                               : !still || !plan.obstacleMoved[0];
            forces += stirred.forceCount != 1 ||
                      !SameLattice(&stirred.forces[0], &turbulence, &box,
                                   (double)stirred.start / (double)MVFX_TICKS_PER_SECOND);
        }
    }
    CHECK(split == 8, "every step split");
    CHECK(cleared == 0, "a clear in the first step's first substep alone");
    CHECK(obstacles == 0, "each substep's obstacles");
    CHECK(forces == 0, "each substep's forces at its start");
    mvfxDestroyContext(context);
}

static void TestSpeedSetter(void)
{
    // The latest report counts, an older one is ignored (the next steps
    // count from it); refused for a smoke gas, a tick out of range, a
    // speed not finite or negative, an instance not planned or gone; a
    // restart forgets the reports.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gases[2] = {Liquid(), mvfxDefaultGasDef()};
    gases[1].size[0] = 4;
    gases[1].size[1] = 4;
    mvfxEffectId effect = Effect(context, gases, 2);
    mvfxInstanceId planned = Instance(context, effect, true);
    mvfxInstanceId cpu = Instance(context, effect, false);
    int32_t count = 0;
    CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
    // 3 a second gives 2 substeps, 5 would give 3.
    int32_t three = LiquidSubsteps(3.0f, &gases[0], 9.81, STEP_TICKS);
    bool latest =
        three != LiquidSubsteps(5.0f, &gases[0], 9.81, STEP_TICKS) &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, STEP_TICKS, 3.0f) == mvfx_success &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, 5.0f) == mvfx_success &&
        mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
        mvfxInstance_GetPlanSteps(context, planned, &count) == mvfx_success && count == three;
    CHECK(latest, "the latest report counts");
    uint64_t misuse = mvfxContext_GetMisuse(context);
    bool refused =
        mvfxInstance_SetLiquidSpeed(NULL, planned, 0, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 1, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 2, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, -1, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 2 * STEP_TICKS + 1, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, -1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, NAN) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, INFINITY) == mvfx_errorInvalid &&
        mvfxContext_GetMisuse(context) == misuse + 7 &&
        mvfxInstance_SetLiquidSpeed(context, cpu, 0, 0, 1.0f) == mvfx_errorUnsupported &&
        mvfxInstance_SetLiquidSpeed(context, (mvfxInstanceId){0}, 0, 0, 1.0f) == mvfx_errorStale;
    // The report kept through the refusals: the next step counts from it.
    CHECK(refused && mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetPlanSteps(context, planned, &count) == mvfx_success && count == three,
          "the speed setter's refusals");
    // A restart forgets the reports: its first step has no particle alive
    // (gravity's count), its next one no report (the most).
    int32_t first = 0;
    CHECK(mvfxInstance_Restart(context, planned) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetPlanSteps(context, planned, &first) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetPlanSteps(context, planned, &count) == mvfx_success &&
              first == LiquidSubsteps(0.0f, &gases[0], 9.81, STEP_TICKS) &&
              count == MVFX_MAX_LIQUID_SUBSTEPS,
          "a restart forgets the reports");
    mvfxDestroyContext(context);
}

static void TestPlanRoom(void)
{
    // A beam of six points living a millisecond beside a liquid gas: a
    // step cut into 16 substeps strikes it in each, 96 births, past the
    // plan's room of twice its capacity; the rest are refused, not
    // written past it.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Liquid();
    mvfxEmitterDef emitters[2] = {Emitter(), mvfxDefaultEmitterDef()};
    emitters[1].rate = 0.0f;
    emitters[1].capacity = 6;
    emitters[1].lifetime = mvfxRangeSource(0.001f, 0.001f);
    emitters[1].ribbon.ids = mvfx_ribbonOne;
    emitters[1].beam = (mvfxBeamDef){
        .enable = true, .segments = 5, .start = {.point = {0, 0, 0}}, .end = {.point = {1, 0, 0}}};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceId planned = Instance(context, effect, true);
    // The liquid's births in the first step; no report, so the most after.
    int32_t count = 0;
    CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success &&
              mvfxInstance_GetPlanSteps(context, planned, &count) == mvfx_success,
          "stepped");
    static mvfxEmitterPlan plan;
    int32_t written = 0;
    uint64_t refused = 0;
    for (int32_t k = 0; k < count; ++k)
    {
        CHECK(mvfxInstance_GetEmitterStepPlan(context, planned, 1, k, &plan) == mvfx_success,
              "a step's plan");
        written += plan.birthCount;
        refused += plan.refused;
    }
    CHECK(count == MVFX_MAX_LIQUID_SUBSTEPS && written == 12 && refused == 16 * 6 - 12,
          "a plan's births past its room refused");
    mvfxDestroyContext(context);
}

// Whether a gas plan's map of a world-space emitter's particles (plan.h,
// mvfxPlannedPour: a place p goes to origin + rotation (sigma p), the
// particles offsets from their anchor after the step) takes them into
// the box the gas's info places: p + anchor back into the box's frame.
static bool SameMap(const mvfxPlannedPour* m, mvfxContext* context, mvfxInstanceId planned,
                    int32_t gas)
{
    static mvfxEmitterPlan plan;
    mvfxGasInfo box;
    CHECK(mvfxInstance_GetEmitterPlan(context, planned, 0, &plan) == mvfx_success &&
              mvfxInstance_GetGasInfo(context, planned, gas, &box) == mvfx_success,
          "the anchor and the box");
    Frame frame = BoxFrame(&box, &plan.to, mvfx_spaceWorld, plan.anchorAfter);
    return SamePour(m, &frame);
}

static void TestLiquidMap(void)
{
    // A liquid gas's plan hands its emitter's particles' map into the box
    // over, under a moved, turned, grown instance; a smoke gas's names
    // none.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gases[2] = {Liquid(), mvfxDefaultGasDef()};
    gases[0].center[0] = 0.3f;
    gases[1].size[0] = 4;
    gases[1].size[1] = 4;
    mvfxEffectId effect = Effect(context, gases, 2);
    mvfxInstanceId planned = Instance(context, effect, true);
    const double origin[3] = {3.0, -1.0, 2.0};
    const float rotation[4] = {0.0f, 0.0f, sinf(0.3f), cosf(0.3f)};
    CHECK(mvfxInstance_SetTransform(context, planned, origin, rotation, 1.5f, false) ==
                  mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success,
          "stepped");
    static mvfxGasPlan plan;
    static mvfxGasPlan smoke;
    CHECK(mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success &&
              mvfxInstance_GetGasPlan(context, planned, 1, &smoke) == mvfx_success,
          "plans");
    bool same = plan.liquid.emitter == 0 && SameMap(&plan.liquid, context, planned, 0) &&
                plan.liquid.rates[0] == 0.0f && plan.liquid.drag == 0.0f;
    CHECK(same && smoke.liquid.emitter == -1, "a liquid gas's plan maps its particles");
    mvfxDestroyContext(context);
}

static void TestSurfaceMap(void)
{
    // A surface gas's plan hands over the map of the emitter it wraps into
    // the box, under a moved, turned, grown instance.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Liquid();
    gas.kind = mvfx_gasSurface;
    gas.surface.emitter = 0;
    gas.surface.radius = 0.1f;
    gas.center[1] = -0.2f;
    mvfxEmitterDef emitter = Emitter();
    emitter.liquid.enable = false;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "a surface gas's effect");
    mvfxInstanceId planned = Instance(context, effect, true);
    const double origin[3] = {-2.0, 1.0, 0.5};
    const float rotation[4] = {sinf(0.2f), 0.0f, 0.0f, cosf(0.2f)};
    if (mvfxInstance_SetTransform(context, planned, origin, rotation, 0.75f, false) !=
            mvfx_success ||
        mvfxContext_StepTicks(context, STEP_TICKS) != mvfx_success)
    {
        CHECK(false, "a surface gas planned and stepped");
        mvfxDestroyContext(context);
        return;
    }
    static mvfxGasPlan plan;
    CHECK(mvfxInstance_GetGasPlan(context, planned, 0, &plan) == mvfx_success, "plan");
    CHECK(plan.liquid.emitter == 0 && SameMap(&plan.liquid, context, planned, 0),
          "a surface gas's plan maps its particles");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestSplit();
    TestGasSteps();
    TestSpeedSetter();
    TestPlanRoom();
    TestLiquidMap();
    TestSurfaceMap();
    return s_failures == 0 ? 0 : 1;
}
