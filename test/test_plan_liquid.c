// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned liquid gases: a planned instance cuts its steps into the
// substeps the CPU path cuts them into, counted from the fastest speed
// its simulation reports (the CPU path's own when reported each step; the
// gravity's alone with no particle alive; the most before any report),
// and hands over a plan a substep: the span, the move over the first
// alone, the births by their substep's start, the serials owed. The speed
// setter's refusals; births past a plan's room refused; a surface gas
// still unplannable.

#include "gas.h"
#include "gas_liquid.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/plan.h"

#include <math.h>

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
    emitter.liquid.enabled = true;
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

// The CPU instance's fastest particle speed now.
static float Fastest(const mvfxInstance* live)
{
    const float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = mvfxAttributeArray((mvfxInstance*)live, 0,
                                  (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    float fastest = 0.0f;
    for (int32_t i = 0; i < live->emitters[0].count; ++i)
    {
        fastest = fmaxf(fastest, v[0][i] * v[0][i] + v[1][i] * v[1][i] + v[2][i] * v[2][i]);
    }
    return sqrtf(fastest);
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
    int64_t done = 0;
    for (int32_t k = 0; k < count; ++k)
    {
        int64_t to = STEP_TICKS * (k + 1) / count;
        CHECK(mvfxInstance_GetEmitterStepPlan(context, planned, 0, k, &plan) == mvfx_success,
              "a step's plan");
        broken->spans += plan.start != start + done || plan.ticks != to - done;
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
    CHECK(mvfxInstance_GetEmitterStepPlan(context, planned, 0, count, &plan) == mvfx_errorInvalid &&
              mvfxInstance_GetEmitterStepPlan(context, planned, 1, 0, &plan) == mvfx_errorInvalid,
          "a step's plan's refusals");
}

// Twins of one seed in one context: the CPU path's and the planned.
typedef struct Twins
{
    mvfxContext* context;
    mvfxInstanceId cpu;
    mvfxInstanceId planned;
} Twins;

// Steps the twins once (step s), the planned one told the CPU one's
// fastest speed first when reporting, and checks its plans.
static void StepTwins(const Twins* t, int32_t s, bool reporting, uint64_t* serial, Broken* broken)
{
    const mvfxInstance* cpuLive = mvfxContextInstance(t->context, t->cpu);
    int64_t start = cpuLive->clock;
    mvfxResult reported =
        reporting ? mvfxInstance_SetLiquidSpeed(t->context, t->planned, 0, start, Fastest(cpuLive))
                  : mvfx_success;
    Move(t->context, t->cpu, s);
    Move(t->context, t->planned, s);
    // Before any report with particles alive, the most.
    int32_t expected =
        s > 0 && !reporting ? MVFX_MAX_LIQUID_SUBSTEPS : mvfxLiquidSubsteps(cpuLive, STEP_TICKS);
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
    broken->owed += mvfxContextInstance(t->context, t->cpu)->emitters[0].owed != *serial;
}

static void TestSplit(void)
{
    // Twins of one seed, the planned one told the CPU one's fastest speed
    // at each step's start from its third step on: its first step has no
    // particle (gravity's count, the CPU path's), its second no report
    // (the most), the rest the CPU path's count.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Twins t = {0};
    CHECK(mvfxCreateContext(&contextDef, &t.context) == mvfx_success, "context");
    mvfxGasDef gas = Liquid();
    mvfxEffectId effect = Effect(t.context, &gas, 1);
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
              mvfxInstance_GetPlanSteps(t.context, t.cpu, &count) == mvfx_errorState &&
              mvfxInstance_GetPlanSteps(t.context, (mvfxInstanceId){0}, &count) == mvfx_errorStale,
          "the steps' refusals");
    mvfxDestroyContext(t.context);
}

static void TestSpeedSetter(void)
{
    // The latest report counts, an older one is ignored; refused for a
    // smoke gas, a tick out of range, a speed not finite or negative, an
    // instance not planned or gone.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gases[2] = {Liquid(), mvfxDefaultGasDef()};
    gases[1].size[0] = 4;
    gases[1].size[1] = 4;
    mvfxEffectId effect = Effect(context, gases, 2);
    mvfxInstanceId planned = Instance(context, effect, true);
    mvfxInstanceId cpu = Instance(context, effect, false);
    CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
    const mvfxGasState* state = &mvfxContextInstance(context, planned)->gases[0];
    bool latest =
        state->reportedTick == -1 &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, STEP_TICKS, 3.0f) == mvfx_success &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, 5.0f) == mvfx_success &&
        state->reportedSpeed == 3.0f && state->reportedTick == STEP_TICKS;
    CHECK(latest, "the latest report counts");
    uint64_t misuse = mvfxGetContextMisuse(context);
    bool refused =
        mvfxInstance_SetLiquidSpeed(NULL, planned, 0, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 1, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 2, 0, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, -1, 1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, STEP_TICKS + 1, 1.0f) ==
            mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, -1.0f) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, NAN) == mvfx_errorInvalid &&
        mvfxInstance_SetLiquidSpeed(context, planned, 0, 0, INFINITY) == mvfx_errorInvalid &&
        mvfxGetContextMisuse(context) == misuse + 7 &&
        mvfxInstance_SetLiquidSpeed(context, cpu, 0, 0, 1.0f) == mvfx_errorState &&
        mvfxInstance_SetLiquidSpeed(context, (mvfxInstanceId){0}, 0, 0, 1.0f) == mvfx_errorStale;
    CHECK(refused && state->reportedSpeed == 3.0f, "the speed setter's refusals");
    // A restart forgets the reports.
    CHECK(mvfxInstance_Restart(context, planned) == mvfx_success && state->reportedTick == -1,
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
        .enabled = true, .segments = 5, .start = {.point = {0, 0, 0}}, .end = {.point = {1, 0, 0}}};
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

static void TestSurfaceRefused(void)
{
    // A surface gas still keeps its effect unplannable.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxGasDef gas = Liquid();
    gas.kind = mvfx_gasSurface;
    gas.surface.radius = 0.1f;
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.planned = true;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_errorState && instance.index1 == 0,
          "a surface gas unplannable");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestSplit();
    TestSpeedSetter();
    TestPlanRoom();
    TestSurfaceRefused();
    return s_failures == 0 ? 0 : 1;
}
