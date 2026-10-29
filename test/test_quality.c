// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Quality and budgets: an instance at a level, its capacities scaled
// (a beam's kept), its rate, distance rate and burst counts scaled, an
// emitter left out refusing the host's spawns; the context's budget
// choosing lower levels and refusing what fits at none, given back at a
// destroy; a restart keeping the level; the checks of levels, of the
// asked level and of the budget; the query's refusals.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

static mvfxContext* NewContext(int64_t budget)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.particles = budget;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    return context;
}

// Emitter 0: 100 particles, 1,000 a second; 1: 40 particles, 10 a second
// and a burst of 7; 2: 20 particles, 2 a unit of travel; 3: a beam of 4
// segments.
static mvfxEmitterDef s_emitters[4];
static const mvfxBurst s_burst = {
    .time = 0.0f, .minCount = 7, .maxCount = 7, .cycles = 1, .probability = 1.0f};
static const mvfxQualityLevel s_levels[2] = {
    {.capacityScale = 0.5f, .rateScale = 0.5f},
    {.capacityScale = 0.25f, .rateScale = 0.0f, .omitted = 1u << 1u},
};

static mvfxEffectId NewEffect(mvfxContext* context)
{
    for (int32_t e = 0; e < 4; ++e)
    {
        s_emitters[e] = mvfxDefaultEmitterDef();
        s_emitters[e].lifetime = mvfxRangeSource(9.0f, 9.0f);
        s_emitters[e].rate = 0.0f;
    }
    s_emitters[0].capacity = 100;
    s_emitters[0].rate = 1000.0f;
    s_emitters[1].capacity = 40;
    s_emitters[1].rate = 10.0f;
    s_emitters[1].bursts = &s_burst;
    s_emitters[1].burstCount = 1;
    s_emitters[1].hostSpawnCapacity = 4;
    s_emitters[2].capacity = 20;
    s_emitters[2].ratePerDistance = 2.0f;
    s_emitters[3].capacity = 5;
    s_emitters[3].ribbon.ids = mvfx_ribbonOne;
    s_emitters[3].beam = (mvfxBeamDef){.enabled = true, .segments = 4, .end = {.point = {4, 0, 0}}};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = s_emitters;
    def.emitterCount = 4;
    def.qualityLevels = s_levels;
    def.qualityLevelCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    return effect;
}

static mvfxResult NewInstance(mvfxContext* context, mvfxEffectId effect, int32_t quality,
                              mvfxInstanceId* out)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.quality = quality;
    return mvfxCreateInstance(context, &def, out);
}

static mvfxEmitterStats Stats(mvfxContext* context, mvfxInstanceId instance, int32_t emitter)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(context, instance, emitter, &stats) == mvfx_success,
          "stats");
    return stats;
}

// Moves instances 2 units along x over a second.
static void Run(mvfxContext* context, const mvfxInstanceId* instances, int32_t count)
{
    static const double there[3] = {2, 0, 0};
    static const float identity[4] = {0, 0, 0, 1};
    for (int32_t i = 0; i < count; ++i)
    {
        CHECK(mvfxInstance_SetTransform(context, instances[i], there, identity, 1.0f, false) ==
                  mvfx_success,
              "moved");
    }
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
}

static void TestLevels(void)
{
    mvfxContext* context = NewContext(0);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceId full = {0};
    mvfxInstanceId half = {0};
    CHECK(NewInstance(context, effect, 0, &full) == mvfx_success &&
              NewInstance(context, effect, 1, &half) == mvfx_success,
          "two instances");
    int32_t level = -1;
    CHECK(mvfxInstance_GetQuality(context, half, &level) == mvfx_success && level == 1,
          "at the level asked");
    // 165 at full; 50 + 20 + 10 + 5 (the beam whole) at half.
    CHECK(mvfxContext_GetParticles(context) == 165 + 85, "the particles held");
    const mvfxInstanceId both[2] = {full, half};
    Run(context, both, 2);
    // Full: 100 of 1,000 (capacity), 10 + 7, 4 of travel, the beam's 5.
    CHECK(Stats(context, full, 0).live == 100 && Stats(context, full, 1).live == 17 &&
              Stats(context, full, 2).live == 4 && Stats(context, full, 3).live == 5,
          "the full level");
    // Half: 50 of 500, 5 + 4 (3.5 rounded up), 2 of travel, the beam's 5.
    CHECK(Stats(context, half, 0).live == 50 && Stats(context, half, 0).owed == 500 &&
              Stats(context, half, 1).live == 9 && Stats(context, half, 2).live == 2 &&
              Stats(context, half, 3).live == 5,
          "capacities and rates halved");
    CHECK(mvfxDestroyInstance(context, full) == mvfx_success &&
              mvfxContext_GetParticles(context) == 85,
          "given back at a destroy");
    mvfxDestroyContext(context);
}

static void TestLeftOut(void)
{
    // Level 2: a quarter of the capacity, no rate, emitter 1 left out.
    mvfxContext* context = NewContext(0);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceId low = {0};
    CHECK(NewInstance(context, effect, 2, &low) == mvfx_success, "low");
    CHECK(mvfxContext_GetParticles(context) == 25 + 0 + 5 + 5, "a quarter, one left out");
    static const double place[3] = {0, 0, 0};
    mvfxSpawnRequest request = {.count = 3, .positions = place};
    static double places[9];
    request.positions = places;
    CHECK(mvfxInstance_Spawn(context, low, 1, &request) == mvfx_success, "spawned into it");
    Run(context, &low, 1);
    mvfxEmitterStats left = Stats(context, low, 1);
    CHECK(left.live == 0 && left.refused == 3 && left.owed == 0, "left out, the host's refused");
    CHECK(Stats(context, low, 0).live == 0 && Stats(context, low, 2).live == 0 &&
              Stats(context, low, 3).live == 5,
          "no rate, the beam still struck");
    CHECK(mvfxInstance_Restart(context, low) == mvfx_success, "restarted");
    int32_t level = -1;
    CHECK(mvfxInstance_GetQuality(context, low, &level) == mvfx_success && level == 2,
          "a restart keeps the level");
    mvfxDestroyContext(context);
}

static void TestBudget(void)
{
    // A budget of 260: the first at full (165), the second down to level
    // 1 (85), the third at none (level 2 holds 35, and 285 is past it).
    mvfxContext* context = NewContext(260);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceId a = {0};
    mvfxInstanceId b = {0};
    mvfxInstanceId c = {0};
    int32_t level = -1;
    CHECK(NewInstance(context, effect, 0, &a) == mvfx_success &&
              mvfxInstance_GetQuality(context, a, &level) == mvfx_success && level == 0,
          "the first at full");
    CHECK(NewInstance(context, effect, 0, &b) == mvfx_success &&
              mvfxInstance_GetQuality(context, b, &level) == mvfx_success && level == 1 &&
              mvfxContext_GetParticles(context) == 250,
          "the second at the first level that fits");
    CHECK(NewInstance(context, effect, 0, &c) == mvfx_errorCapacity && c.index1 == 0 &&
              mvfxContext_GetParticles(context) == 250,
          "none fits: refused");
    CHECK(mvfxDestroyInstance(context, b) == mvfx_success &&
              NewInstance(context, effect, 2, &c) == mvfx_success &&
              mvfxInstance_GetQuality(context, c, &level) == mvfx_success && level == 2 &&
              mvfxContext_GetParticles(context) == 200,
          "room again after a destroy");
    mvfxDestroyContext(context);
}

static void TestEdges(void)
{
    // A budget the second fits exactly at level 1 (165 + 85 = 250).
    mvfxContext* context = NewContext(250);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceId a = {0};
    mvfxInstanceId b = {0};
    int32_t level = -1;
    CHECK(NewInstance(context, effect, 0, &a) == mvfx_success &&
              NewInstance(context, effect, 0, &b) == mvfx_success &&
              mvfxInstance_GetQuality(context, b, &level) == mvfx_success && level == 1,
          "a budget met exactly");
    mvfxDestroyContext(context);
    // One looping emitter of 10 a second: finished at a level without its
    // rate or without it, not at the full one; a tiny scale keeps one.
    context = NewContext(0);
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 40;
    emitter.rate = 10.0f;
    emitter.duration = 1.0f;
    static const mvfxQualityLevel levels[3] = {
        {.capacityScale = 1.0f, .rateScale = 0.0f},
        {.capacityScale = 1.0f, .rateScale = 1.0f, .omitted = 1u},
        {.capacityScale = 0.001f, .rateScale = 1.0f},
    };
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.qualityLevels = levels;
    def.qualityLevelCount = 3;
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "looping effect");
    mvfxInstanceId at[4] = {{0}};
    for (int32_t q = 0; q < 4; ++q)
    {
        CHECK(NewInstance(context, effect, q, &at[q]) == mvfx_success, "an instance a level");
    }
    CHECK(mvfxContext_GetParticles(context) == 40 + 40 + 0 + 1, "a tiny scale keeps one");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 10) == mvfx_success, "step");
    CHECK(!mvfxInstance_IsFinished(context, at[0]) && mvfxInstance_IsFinished(context, at[1]) &&
              mvfxInstance_IsFinished(context, at[2]) && !mvfxInstance_IsFinished(context, at[3]),
          "finished without a rate or without the emitter");
    mvfxDestroyContext(context);
}

static void TestChecks(void)
{
    mvfxContext* context = NewContext(0);
    NewEffect(context);
    mvfxQualityLevel levels[2] = {s_levels[0], s_levels[1]};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = s_emitters;
    def.emitterCount = 4;
    def.qualityLevels = levels;
    def.qualityLevelCount = 2;
    mvfxEffectId effect = {0};
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 9; ++rule)
    {
        levels[0] = s_levels[0];
        def.qualityLevels = levels;
        def.qualityLevelCount = 2;
        switch (rule)
        {
        case 0:
            def.qualityLevelCount = MVFX_MAX_QUALITY_LEVELS + 1;
            break;
        case 1:
            def.qualityLevelCount = -1;
            break;
        case 2:
            def.qualityLevels = NULL;
            break;
        case 3:
            levels[0].capacityScale = 0.0f;
            break;
        case 4:
            levels[0].capacityScale = 1.5f;
            break;
        case 5:
            levels[0].capacityScale = (float)NAN;
            break;
        case 6:
            levels[0].rateScale = -0.1f;
            break;
        case 7:
            levels[0].rateScale = 1.1f;
            break;
        default:
            levels[0].omitted = 1u << 4u;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 0 ? mvfx_errorLimit : mvfx_errorInvalid;
        refused += mvfxCreateEffect(context, &def, &effect) == want;
    }
    CHECK(refused == 9, "every level check");
    levels[0] = (mvfxQualityLevel){.capacityScale = 1.0f, .rateScale = 1.0f, .omitted = 15u};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the bounds of each");
    mvfxInstanceId instance = {0};
    uint64_t misuse = mvfxGetContextMisuse(context);
    CHECK(NewInstance(context, effect, 3, &instance) == mvfx_errorInvalid &&
              NewInstance(context, effect, -1, &instance) == mvfx_errorInvalid &&
              mvfxGetContextMisuse(context) == misuse + 2,
          "a level it does not have, counted");
    CHECK(NewInstance(context, effect, 2, &instance) == mvfx_success, "its last level");
    int32_t level = 0;
    CHECK(mvfxInstance_GetQuality(context, instance, NULL) == mvfx_errorInvalid &&
              mvfxInstance_GetQuality(NULL, instance, &level) == mvfx_errorInvalid,
          "the query refused");
    mvfxInstanceId stale = instance;
    stale.generation += 1;
    CHECK(mvfxInstance_GetQuality(context, stale, &level) == mvfx_errorStale, "stale");
    mvfxDestroyContext(context);
    // A budget from 0 to 2^40.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* other = NULL;
    contextDef.limits.particles = -1;
    CHECK(mvfxCreateContext(&contextDef, &other) == mvfx_errorInvalid, "a negative budget");
    contextDef.limits.particles = ((int64_t)1 << 40) + 1;
    CHECK(mvfxCreateContext(&contextDef, &other) == mvfx_errorInvalid, "too large a budget");
    contextDef.limits.particles = (int64_t)1 << 40;
    CHECK(mvfxCreateContext(&contextDef, &other) == mvfx_success, "the largest budget");
    mvfxDestroyContext(other);
}

int main(void)
{
    TestLevels();
    TestLeftOut();
    TestBudget();
    TestEdges();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
